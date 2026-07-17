/**
 * @file    uart_parse.c
 * @brief   UART protocol parser implementation
 * @brief   UART 协议解析层实现
 *
 * 状态机:
 *   WAIT_SOF  ->  收到 SOF 进入 READ_LEN
 *   READ_LEN  ->  读到 1B LEN,合法则进入 READ_DATA,否则回 WAIT_SOF
 *   READ_DATA ->  收 LEN 个字节,进入 READ_CRC
 *   READ_CRC  ->  收 1B CRC,校验通过进入 READ_EOF,否则回 WAIT_SOF
 *   READ_EOF  ->  收 1B EOF,匹配则分发到 RECEIVE_DATA_A0
 *
 * 本文件不依赖任何 SDK / 硬件头,只通过 uart.h 与硬件层通信。
 */
#include "uart_parse.h"
#include "uart.h"

#define MOD_NAME  "UPR"
#include "log.h"

#ifndef UART_PARSE_USE_XOR_CRC
#define UART_PARSE_USE_XOR_CRC  1   /* 默认 XOR 校验,置 0 改用 CRC-8 */
#endif

/* 内部状态 ------------------------------------------------------------- */
typedef enum {
    PARSE_STATE_WAIT_SOF = 0,
    PARSE_STATE_READ_LEN,
    PARSE_STATE_READ_DATA,
    PARSE_STATE_READ_CRC,
    PARSE_STATE_READ_EOF,
} parse_state_t;

static parse_state_t s_state = PARSE_STATE_WAIT_SOF;

static uint8  s_data_buf[UART_PARSE_MAX_DATA];
static uint8  s_data_len;
static uint8  s_data_idx;
static uint8  s_crc_rx;
static uint8  s_crc_calc;

static uint32 s_idle_ticks = 0U;
static uint32 s_frame_timeout = UART_PARSE_FRAME_TIMEOUT_TICKS;

/* 统计(便于调试) */
static uint32 s_stat_total_frames;
static uint32 s_stat_bad_crc;
static uint32 s_stat_bad_eof;
static uint32 s_stat_bad_len;
static uint32 s_stat_timeout;

/* CRC 算法 ------------------------------------------------------------- */
static uint8 Uart_Parse_ComputeCrc(const uint8 *data, uint32 len)
{
#if UART_PARSE_USE_XOR_CRC
    uint8 crc = 0U;
    for (uint32 i = 0U; i < len; ++i) {
        crc ^= data[i];
    }
    return crc;
#else
    /* CRC-8/SMBUS: poly=0x07 init=0x00 */
    uint8 crc = 0U;
    for (uint32 i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint8 b = 0U; b < 8U; ++b) {
            crc = (uint8)((crc & 0x80U) ? ((crc << 1) ^ 0x07U) : (crc << 1));
        }
    }
    return crc;
#endif
}

/* 业务层钩子默认实现(weak):
 *   - IAR:        #pragma weak
 *   - GCC/ARMCC:  __attribute__((weak))
 * 用户可在业务模块的 .c 文件中重新定义 RECEIVE_DATA_A0(非 weak),
 * 链接器自动选用业务版实现。
 * --------------------------------------------------------------------- */
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

/* 状态机分发 ----------------------------------------------------------- */
static void Uart_Parse_DispatchValidFrame(void)
{
    s_stat_total_frames++;
    LOG_D("frame ok len=%u", s_data_len);
    RECEIVE_DATA_A0(s_data_buf, s_data_len);
}

/* 由 uart.c 通过 Uart_RegisterRxByteHandler() 回调进来 --------------- */
static void Uart_Parse_OnRxByte(uint8 byte, void *ctx)
{
    (void)ctx;
    s_idle_ticks = 0U;  /* 有字节到达,清零帧间隔 */

    switch (s_state) {
    case PARSE_STATE_WAIT_SOF:
        if (byte == UART_PARSE_SOF) {
            s_state = PARSE_STATE_READ_LEN;
        }
        /* 否则继续丢弃,寻找 SOF */
        break;

    case PARSE_STATE_READ_LEN:
        if ((byte == 0U) || (byte > UART_PARSE_MAX_DATA)) {
            s_stat_bad_len++;
            LOG_W("bad len=%u", byte);
            s_state = PARSE_STATE_WAIT_SOF;
            break;
        }
        s_data_len = byte;
        s_data_idx = 0U;
        s_state    = PARSE_STATE_READ_DATA;
        break;

    case PARSE_STATE_READ_DATA:
        s_data_buf[s_data_idx++] = byte;
        if (s_data_idx >= s_data_len) {
            s_state = PARSE_STATE_READ_CRC;
        }
        break;

    case PARSE_STATE_READ_CRC:
        s_crc_rx   = byte;
        s_crc_calc = Uart_Parse_ComputeCrc(s_data_buf, s_data_len);
        if (s_crc_rx != s_crc_calc) {
            s_stat_bad_crc++;
            LOG_W("crc mismatch rx=0x%02X calc=0x%02X",
                  s_crc_rx, s_crc_calc);
            s_state = PARSE_STATE_WAIT_SOF;
            break;
        }
        s_state = PARSE_STATE_READ_EOF;
        break;

    case PARSE_STATE_READ_EOF:
        if (byte == UART_PARSE_EOF) {
            Uart_Parse_DispatchValidFrame();
        } else {
            s_stat_bad_eof++;
            LOG_W("bad eof=0x%02X", byte);
        }
        s_state = PARSE_STATE_WAIT_SOF;
        break;

    default:
        s_state = PARSE_STATE_WAIT_SOF;
        break;
    }
}

/* 公共 API ------------------------------------------------------------- */
c02b2_result_t Uart_Parse_Init(void)
{
    Uart_Parse_Reset();
    Uart_RegisterRxByteHandler(Uart_Parse_OnRxByte, NULL);
    LOG_I("parse init ok, sof=0x%02X eof=0x%02X max=%u",
          UART_PARSE_SOF, UART_PARSE_EOF, UART_PARSE_MAX_DATA);
    return C02B2_OK;
}

void Uart_Parse_Reset(void)
{
    s_state      = PARSE_STATE_WAIT_SOF;
    s_data_len   = 0U;
    s_data_idx   = 0U;
    s_crc_rx     = 0U;
    s_crc_calc   = 0U;
    s_idle_ticks = 0U;
}

void Uart_Parse_Tick(uint32 elapsed_ticks)
{
    if ((s_state == PARSE_STATE_WAIT_SOF) && (s_idle_ticks == 0U)) {
        /* 空闲态无须推进 */
        return;
    }

    s_idle_ticks += elapsed_ticks;
    if (s_idle_ticks >= s_frame_timeout) {
        if (s_state != PARSE_STATE_WAIT_SOF) {
            s_stat_timeout++;
            LOG_W("frame timeout, reset state=%d", (int)s_state);
        }
        Uart_Parse_Reset();
    }
}
