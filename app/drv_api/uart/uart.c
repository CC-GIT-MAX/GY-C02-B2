/**
 * @file    uart.c
 * @brief   UART hardware abstraction implementation
 * @brief   UART 硬件抽象层实现
 *
 * 职责范围(严格限定):
 *   - 调用 SDK LINFlexD 驱动完成硬件初始化
 *   - 安装 SDK RX/TX/ERR 回调
 *   - 把 SDK 上报的接收字节推入本文件内的软件环形缓冲
 *   - 把环形缓冲中的字节逐个投递给通过 Uart_RegisterRxByteHandler()
 *     注册的订阅者(典型为 uart_parse.c 的协议解析模块)
 *
 * 本文件 **不** 包含任何协议解析/帧识别/CRC 校验逻辑。
 * 协议解析完全在 uart_parse.c 中实现,通过 Uart_RegisterRxByteHandler()
 * 接口与本文件解耦。
 *
 * SDK 接收机制说明:
 *   SDK 不论是中断模式还是 DMA 模式,只要我们传入一个 size=1 的缓冲区,
 *   每收到 1 个字节就会:
 *     - 在缓冲区[0]位置写入该字节
 *     - 触发 rxCallback(UART_EVENT_END_TRANSFER)
 *   本回调把 s_rx_byte_buf[0] 取走,再调用 LINFlexD_UART_DRV_ReceiveData()
 *   重新提交 1 字节接收。
 *
 *   这样无论 board 配置为 LINFlexD_UART_USING_INTERRUPTS 还是 DMA,
 *   都能得到逐字节的事件,无需修改 board 层配置。
 */
#include "uart.h"
#include "sdk_project_config.h"

#include <stddef.h>

#define MOD_NAME  "UAR"
#include "log.h"

/* board/linflexd_uart_config.c 提供的外部符号 ------------------------- */
extern linflexd_uart_state_t      COMM_uart_config_State;
extern const linflexd_uart_user_config_t COMM_uart_config;

/* ------------------------------------------------------------------------ *
 *  1 字节 RX 缓冲(由 SDK 写入,回调中读走)
 * ------------------------------------------------------------------------ */
static uint8 s_rx_byte_buf[1];

/* ------------------------------------------------------------------------ *
 *  软件环形缓冲:用于在中断上下文与上层投递之间解耦
 *  - 仅本文件可见,不向协议解析层暴露
 *  - 协议解析层仅通过 Uart_RegisterRxByteHandler() 接收字节
 *  - 深度取 2 的幂便于 % 优化
 * ------------------------------------------------------------------------ */
#define UART_RX_RING_SIZE   256U

typedef struct {
    uint8              buf[UART_RX_RING_SIZE];
    volatile uint32    head;   /* 写指针(SDK 回调上下文) */
    volatile uint32    tail;   /* 读指针(投递上下文)     */
} uart_rx_ring_t;

static uart_rx_ring_t    s_rx_ring;
static uart_rx_byte_cb_t s_rx_byte_cb  = NULL;
static void             *s_rx_byte_ctx = NULL;

/* 防止 SDK 回调重入投递逻辑的简易开关 */
static volatile uint8    s_in_drain = 0U;

/* ------------------------------------------------------------------------ *
 *  私有:把字节投递到订阅者(只在 SDK 回调上下文调用)
 * ------------------------------------------------------------------------ */
static void Uart_PushRxByte(uint8 byte)
{
    uint32 next = (s_rx_ring.head + 1U) & (UART_RX_RING_SIZE - 1U);
    if (next == s_rx_ring.tail) {
        /* 缓冲满:丢老字节,记录告警 */
        LOG_W("rx ring overflow, drop byte 0x%02X", s_rx_ring.buf[s_rx_ring.head]);
        s_rx_ring.tail = (s_rx_ring.tail + 1U) & (UART_RX_RING_SIZE - 1U);
    }
    s_rx_ring.buf[s_rx_ring.head] = byte;
    s_rx_ring.head = next;
}

/* 私有:把已入队的字节逐个投递给订阅者(可被 SDK 回调与重入) */
static void Uart_DrainRxRing(void)
{
    if (s_in_drain != 0U) {
        return;
    }
    s_in_drain = 1U;
    while (s_rx_ring.tail != s_rx_ring.head) {
        uint8 b = s_rx_ring.buf[s_rx_ring.tail];
        s_rx_ring.tail = (s_rx_ring.tail + 1U) & (UART_RX_RING_SIZE - 1U);
        if (s_rx_byte_cb != NULL) {
            s_rx_byte_cb(b, s_rx_byte_ctx);
        }
    }
    s_in_drain = 0U;
}

/* ------------------------------------------------------------------------ *
 *  SDK 回调实现
 * ------------------------------------------------------------------------ */
void Uart_RxCallback(void *driverState, uart_event_t event, void *userData)
{
    (void)driverState;
    (void)userData;

    if (event == UART_EVENT_END_TRANSFER) {
        /* 把字节推入环形缓冲,并立即尝试投递给订阅者 */
        Uart_PushRxByte(s_rx_byte_buf[0]);
        Uart_DrainRxRing();

        /* 重新启动下一字节接收 */
        (void)LINFlexD_UART_DRV_ReceiveData(0U, s_rx_byte_buf, 1U);
    } else if (event == UART_EVENT_ERROR) {
        LOG_E("rx err ev=0x%X", (uint32)event);
        /* 错误后重新启动接收 */
        (void)LINFlexD_UART_DRV_ReceiveData(0U, s_rx_byte_buf, 1U);
    } else {
        /* RX_FULL 在 size=1 时与 END_TRANSFER 配对,此处忽略 */
    }
}

void Uart_TxCallback(void *driverState, uart_event_t event, void *userData)
{
    (void)driverState;
    (void)userData;
    if (event == UART_EVENT_END_TRANSFER) {
        /* 发送完成,此处可唤醒等待发送的任务 */
    } else if (event == UART_EVENT_ERROR) {
        LOG_E("tx err ev=0x%X", (uint32)event);
    } else {
        /* ignore */
    }
}

void Uart_ErrCallback(void *driverState, uart_event_t event, void *userData)
{
    (void)driverState;
    (void)userData;
    LOG_E("uart err ev=0x%X", (uint32)event);
}

/* ------------------------------------------------------------------------ *
 *  公共接口
 * ------------------------------------------------------------------------ */
c02b2_result_t Uart_Init(void)
{
    status_t sdk_status;

    sdk_status = LINFlexD_UART_DRV_Init(0U, &COMM_uart_config_State, &COMM_uart_config);
    if (sdk_status != STATUS_SUCCESS) {
        LOG_E("LINFlexD_UART_DRV_Init fail=%d", sdk_status);
        return C02B2_ERR;
    }

    /* 安装 RX/TX/ERR 回调 */
    (void)LINFlexD_UART_DRV_InstallRxCallback(0U, Uart_RxCallback, NULL);
    (void)LINFlexD_UART_DRV_InstallTxCallback(0U, Uart_TxCallback, NULL);
    (void)LINFlexD_UART_DRV_InstallErrorCallback(0U, Uart_ErrCallback, NULL);

    /* 启动 1 字节后台接收 */
    sdk_status = LINFlexD_UART_DRV_ReceiveData(0U, s_rx_byte_buf, 1U);
    if (sdk_status != STATUS_SUCCESS) {
        LOG_E("LINFlexD_UART_DRV_ReceiveData fail=%d", sdk_status);
        return C02B2_ERR;
    }

    s_rx_ring.head   = 0U;
    s_rx_ring.tail   = 0U;
    s_rx_byte_cb     = NULL;
    s_rx_byte_ctx    = NULL;
    s_in_drain       = 0U;

    return C02B2_OK;
}

c02b2_result_t Uart_Send(const uint8 *buf, uint32 len)
{
    if ((buf == NULL) || (len == 0U)) {
        return C02B2_ERR_PARAM;
    }

    if (COMM_uart_config_State.isTxBusy) {
        return C02B2_ERR_BUSY;
    }

    status_t sdk_status = LINFlexD_UART_DRV_SendData(0U, buf, len);
    if (sdk_status != STATUS_SUCCESS) {
        LOG_E("SendData fail=%d", sdk_status);
        return C02B2_ERR;
    }
    return C02B2_OK;
}

c02b2_result_t Uart_SendBlocking(const uint8 *buf, uint32 len, uint32 timeout_ms)
{
    if ((buf == NULL) || (len == 0U)) {
        return C02B2_ERR_PARAM;
    }

    status_t sdk_status = LINFlexD_UART_DRV_SendDataBlocking(0U, buf, len, timeout_ms);
    if (sdk_status != STATUS_SUCCESS) {
        LOG_W("SendDataBlocking fail=%d", sdk_status);
        return C02B2_ERR_TIMEOUT;
    }
    return C02B2_OK;
}

void Uart_RegisterRxByteHandler(uart_rx_byte_cb_t cb, void *ctx)
{
    s_rx_byte_cb  = cb;
    s_rx_byte_ctx = ctx;
}
