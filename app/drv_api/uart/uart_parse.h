/**
 * @file    uart_parse.h
 * @brief   UART protocol parser (isolated from hardware layer)
 * @brief   UART 协议解析层(与硬件层解耦)
 *
 * 本模块只负责:
 *   1. 订阅 UART 硬件抽象层(uart.h)提供的逐字节 RX 回调
 *   2. 在状态机中拼装帧,做 CRC 校验
 *   3. 一帧校验通过后,调用业务层钩子 RECEIVE_DATA_A0()
 *
 * 本模块 **不** 直接访问硬件,不引用 LINFlexD SDK API,
 * 与 uart.c 之间仅通过 Uart_RegisterRxByteHandler() 接口耦合。
 *
 * 协议格式(参考 MCAL 设计,可在本文件中调整):
 *
 *   +------+------+----------+------+--------+
 *   | SOF  | LEN  | DATA     | CRC8 | EOF    |
 *   | 0xAA | 1B   | LEN 字节 | 1B   | 0x55   |
 *   +------+------+----------+------+--------+
 *
 *   - SOF  = 0xAA (起始字节)
 *   - LEN  = DATA 域字节数(1..UART_PARSE_MAX_DATA)
 *   - DATA = 业务负载
 *   - CRC8 = 对 LEN 与 DATA 做 XOR 校验(多项式 0x07 的 CRC-8/SMBUS
 *           也可以在 UART_PARSE_USE_XOR_CRC 切换为 0 时启用;
 *           默认 XOR,实现简单且足以应对稳定链路)
 *   - EOF  = 0x55 (帧结束标记)
 *
 * 若实际业务协议与本默认不符,只需修改本文件中的:
 *   - UART_PARSE_SOF / UART_PARSE_EOF / UART_PARSE_MAX_DATA
 *   - Uart_Parse_ComputeCrc() / Uart_Parse_VerifyCrc()
 */
#ifndef C02B2_DRV_API_UART_PARSE_H
#define C02B2_DRV_API_UART_PARSE_H

#include <stdint.h>
#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 协议常量 -------------------------------------------------------------- */
#define UART_PARSE_SOF            (0xAAU)
#define UART_PARSE_EOF            (0x55U)
#define UART_PARSE_MAX_DATA       (64U)     /**< DATA 域最大长度 */

/* 帧间隔超时:连续两次 byte 时间间隔超过该 tick 数视为帧异常并复位。
 * 由调度器周期性调用 Uart_Parse_Tick() 推进,具体 tick 周期由调用者定义。 */
#define UART_PARSE_FRAME_TIMEOUT_TICKS   (50U)

/**
 * @brief   初始化协议解析模块
 * @brief   Initialize the parser module
 *
 * @details 调用 Uart_RegisterRxByteHandler() 向 UART 硬件层注册自己。
 *          必须在 Uart_Init() 之后调用。
 *
 * @return  c02b2_result_t    C02B2_OK
 */
c02b2_result_t Uart_Parse_Init(void);

/**
 * @brief   解析层周期 tick,由调度器调用
 * @brief   Parser periodic tick (called from scheduler)
 *
 * @details 推进帧间隔超时计数器;若在 UART_PARSE_FRAME_TIMEOUT_TICKS
 *          个 tick 内未收到下一个字节,复位状态机。
 *
 * @param[in]  elapsed_ticks  自上次调用以来经过的 tick 数(>=1)
 */
void Uart_Parse_Tick(uint32 elapsed_ticks);

/**
 * @brief   复位解析状态机
 * @brief   Reset the parser state machine
 */
void Uart_Parse_Reset(void);

/**
 * @brief   业务层钩子:成功解析一帧后被调用
 * @brief   Business-side hook: called when a frame is successfully parsed
 *
 * @details 弱符号默认实现为空。用户可在业务模块中重新定义(去掉 weak)
 *          以接收解析后的数据,签名必须保持一致:
 *            void RECEIVE_DATA_A0(uint8 *buffer, uint8 len);
 *
 * @param[in]  buffer  DATA 域指针(指向内部缓冲,仅本调用期间有效)
 * @param[in]  len     DATA 域长度,1..UART_PARSE_MAX_DATA
 *
 * @note    若业务需要长期持有数据,请自行拷贝。
 */
void RECEIVE_DATA_A0(uint8 *buffer, uint8 len);

#ifdef __cplusplus
}
#endif

#endif /* C02B2_DRV_API_UART_PARSE_H */
