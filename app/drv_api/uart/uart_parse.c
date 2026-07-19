/**
 * @file    uart_parse.c
 * @brief   UART ICom protocol parser implementation (buffer-scan, zero-loss)
 * @brief   UART ICom 协议解析层实现 (环形缓冲+扫描,零丢帧)
 *
 * 架构说明:
 *   与典型的逐字节状态机不同,本实现采用环形缓冲 + 扫描模式:
 *
 *   1. 每个字节入 ISR 时,先写入环形缓冲 (容量 PARSE_BUF_SIZE,32 字节)
 *   2. 然后从缓冲头开始尝试解析一帧
 *   3. 若解析成功:分发到 RECEIVE_DATA_A0,缓冲前进 frame_size 字节
 *   4. 若解析失败 (HEAD 不匹配/坏 ACK/坏 LEN/坏校验):缓冲前进 1 字节,重试
 *   5. 若数据不足:等待更多字节
 *
 *   该架构的核心优势:**任何错误路径都只前进 1 字节**,不会"吞掉"已消费的字节。
 *   这意味着噪声触发的假 HEAD 即使消耗了真帧的开头部分,真帧的 HEAD 仍保留在
 *   缓冲中,会在后续扫描中被识别。
 *
 *   协议格式:
 *   +------+------+------+------+----------+----------+
 *   | HEAD | ACK  | LEN  | CMD  | DATA     | CHECKSUM |
 *   | 2B   | 1B   | 1B   | 1B   | LEN B    | 2B       |
 *   +------+------+------+------+----------+----------+
 *   HEAD = 0xA5, 0x5A  LEN = DATA 字节数  checksum = ~sum+1,uint16 大端
 *
 *   内存占用: 32B 缓冲 + ~200B 代码 + 几字节状态。运行时栈 0。
 */
#include "uart_parse.h"
#include "uart.h"

#define MOD_NAME  "UPR"
#include "log.h"

/* 缓冲容量:必须 >= MAX_FRAME_SIZE (2+1+1+1+20+2 = 27)。32 留余量。*/
#define PARSE_BUF_SIZE  (32U)

/* 环形缓冲 ----------------------------------------------------------- */
static uint8 s_buf[PARSE_BUF_SIZE];
static uint8 s_buf_count;  /* 当前有效字节数 (0..PARSE_BUF_SIZE) */

/* payload 区:CMD + DATA,作为 RECEIVE_DATA_A0 回调的参数 buffer */
static uint8 s_payload[1U + UART_PARSE_MAX_DATA];

static uint32 s_idle_ticks = 0U;
static uint32 s_frame_timeout = UART_PARSE_FRAME_TIMEOUT_TICKS;

/* 统计(便于调试) */
static uint32 s_stat_total_frames;
static uint32 s_stat_bad_head;
static uint32 s_stat_bad_ack;
static uint32 s_stat_bad_len;
static uint32 s_stat_bad_chk;
static uint32 s_stat_timeout;

/* 缓冲 push:满时丢弃最旧字节 (整缓冲左移 1 字节,O(N) 但 N=32 可忽略) */
static void Uart_Parse_BufPush(uint8 byte)
{
    if (s_buf_count < PARSE_BUF_SIZE) {
        s_buf[s_buf_count++] = byte;
        return;
    }
    /* 满:左移 1 字节,丢弃最旧 */
    for (uint8 i = 1U; i < PARSE_BUF_SIZE; ++i) {
        s_buf[i - 1U] = s_buf[i];
    }
    s_buf[PARSE_BUF_SIZE - 1U] = byte;
    /* count 不变 */
}

/* 缓冲 skip:跳过前 n 字节 (左移) */
static void Uart_Parse_BufSkip(uint8 n)
{
    if (n >= s_buf_count) {
        s_buf_count = 0U;
        return;
    }
    for (uint8 i = 0U; i < (uint8)(s_buf_count - n); ++i) {
        s_buf[i] = s_buf[n + i];
    }
    s_buf_count = (uint8)(s_buf_count - n);
}

/* 累加和校验:对 [ACK..最后一字节 DATA] 求和后取反加 1 --------------- */
static uint16 Uart_Parse_ComputeChecksum(const uint8 *buf, uint32 len)
{
    uint16 sum = 0U;
    for (uint32 i = 0U; i < len; ++i) {
        sum += buf[i];
    }
    return (uint16)(~sum + 1U);
}

/* 业务层钩子默认实现(weak):
 *   - IAR:        #pragma weak
 *   - GCC/ARMCC:  __attribute__((weak))
 * 用户可在业务模块的 .c 文件中重新定义 RECEIVE_DATA_A0(非 weak),
 * 链接器自动选用业务版实现。*/
#if defined(__IAR_SYSTEMS_ICC__)
  #pragma weak RECEIVE_DATA_A0
  void RECEIVE_DATA_A0(uint8 *buffer, uint8 len)
#else
  __attribute__((weak)) void RECEIVE_DATA_A0(uint8 *buffer, uint8 len)
#endif
{
    (void)buffer;
    (void)len;
    LOG_D("RECEIVE_DATA_A0 default weak impl, len=%u", len);
}

/* 尝试从缓冲头 (s_buf[0]) 解析一帧。
 * 返回值:
 *   0  = 缓冲数据不足,需要更多字节
 *   N  = 解析了 N 字节 (N=1 表示跳过 1 字节的失败,N=frame_size 表示成功)
 */
static uint8 Uart_Parse_TryFromStart(void)
{
    /* 至少需要 HEAD(2B) 才能开始判断 */
    if (s_buf_count < 2U) {
        return 0U;
    }
    if (s_buf[0] != UART_PARSE_HEAD_BYTE0) {
        return 1U;  /* 不是 HEAD,跳 1 字节 */
    }
    if (s_buf[1] != UART_PARSE_HEAD_BYTE1) {
        return 1U;  /* HEAD[0] 匹配但 HEAD[1] 不匹配,跳 1 字节 */
    }

    /* HEAD 匹配,需要 ACK + LEN + CMD 才能确定 frame size */
    if (s_buf_count < 5U) {
        return 0U;
    }

    const uint8 ack = s_buf[2];
    if (ack >= 2U) {
        s_stat_bad_ack++;
        LOG_W("bad ack=0x%02X", ack);
        return 1U;
    }

    const uint8 len = s_buf[3];
    if (len > UART_PARSE_MAX_DATA) {
        s_stat_bad_len++;
        LOG_W("bad len=%u", len);
        return 1U;
    }

    /* frame_size = HEAD(2) + ACK(1) + LEN(1) + CMD(1) + DATA(LEN) + CHECKSUM(2) */
    const uint8 frame_size = (uint8)(len + 7U);
    if (s_buf_count < frame_size) {
        return 0U;  /* 等待更多字节 */
    }

    /* 校验:累加 ACK + LEN + CMD + DATA = 3 + LEN 字节 */
    const uint16 calc = Uart_Parse_ComputeChecksum(&s_buf[2], (uint32)(3U + len));
    const uint16 rx = (uint16)((uint16)s_buf[5U + len] << 8)
                    | (uint16)s_buf[6U + len];

    if (calc != rx) {
        s_stat_bad_chk++;
        LOG_W("checksum mismatch calc=0x%04X rx=0x%04X (skip 1)", calc, rx);
        return 1U;  /* 关键:校验失败只前进 1 字节,保留 HEAD[0] 之后的真帧头 */
    }

    /* 解析成功:复制 payload,分发 */
    s_stat_total_frames++;
    const uint8 cmd = s_buf[4];
    s_payload[0] = cmd;
    for (uint8 i = 0U; i < len; ++i) {
        s_payload[1U + i] = s_buf[5U + i];
    }
    LOG_D("frame ok ack=%u len=%u cmd=0x%02X", ack, len, cmd);
    RECEIVE_DATA_A0(s_payload, (uint8)(len + 1U));

    return frame_size;
}

/* 由 uart.c 通过 Uart_RegisterRxByteHandler() 回调进来 --------------- */
static void Uart_Parse_OnRxByte(uint8 byte, void *ctx)
{
    (void)ctx;
    s_idle_ticks = 0U;

    /* 1) 写入环形缓冲 */
    Uart_Parse_BufPush(byte);

    /* 2) 循环解析:每成功解析一帧 / 每失败一次跳 1 字节,直到数据不足 */
    uint8 advance;
    while ((advance = Uart_Parse_TryFromStart()) > 0U) {
        Uart_Parse_BufSkip(advance);
    }
}

/* 公共 API ------------------------------------------------------------ */
c02b2_result_t Uart_Parse_Init(void)
{
    Uart_Parse_Reset();
    Uart_RegisterRxByteHandler(Uart_Parse_OnRxByte, NULL);
    LOG_I("parse init ok (ICom, buffer-scan), head=0x%02X%02X max_data=%u buf=%u",
          UART_PARSE_HEAD_BYTE0, UART_PARSE_HEAD_BYTE1,
          UART_PARSE_MAX_DATA, (uint32)PARSE_BUF_SIZE);
    return C02B2_OK;
}

void Uart_Parse_Reset(void)
{
    s_buf_count  = 0U;
    s_idle_ticks = 0U;
    /* 统计保留,便于跨帧观察 */
}

void Uart_Parse_Tick(uint32 elapsed_ticks)
{
    if ((s_buf_count == 0U) && (s_idle_ticks == 0U)) {
        return;  /* 空闲态无超时 */
    }

    s_idle_ticks += elapsed_ticks;
    if (s_idle_ticks >= s_frame_timeout) {
        if (s_buf_count > 0U) {
            s_stat_timeout++;
            LOG_W("parse timeout, dropping %u bytes", (uint32)s_buf_count);
        }
        Uart_Parse_Reset();
    }
}
