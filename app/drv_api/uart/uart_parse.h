/**
 * @file    uart_parse.h
 * @brief   UART ICom protocol parser (isolated from hardware layer)
 * @brief   UART ICom 协议解析层（与硬件层解耦）
 *
 * 本模块只负责:
 *   1. 订阅 UART 硬件抽象层 (uart.h) 提供的逐字节 RX 回调
 *   2. 在环形缓冲中维护最近 32 字节,按需扫描寻找 ICom 帧
 *   3. 一帧校验通过后,调用业务层钩子 RECEIVE_DATA_A0()
 *
 * 本模块不直接访问硬件,不引用任何 LINFlexD SDK API,
 * 与 uart.c 之间仅通过 Uart_RegisterRxByteHandler() 接口解耦。
 *
 * 协议格式（取自 jac_x6c04a APP_PJ/Cluster/Communication/RECV_UART.c）:
 *
 *   +------+------+------+------+----------+----------+
 *   | HEAD | ACK  | LEN  | CMD  | DATA     | CHECKSUM |
 *   | 2B   | 1B   | 1B   | 1B   | LEN B    | 2B       |
 *   +------+------+------+------+----------+----------+
 *
 *   - HEAD     = 0xA5, 0x5A  (little-endian: 0x5AA5)
 *   - ACK      = 0: 数据帧, 1: 应答帧; 必须 < 2
 *   - LEN      = DATA 字段字节数 (0..UART_PARSE_MAX_DATA)
 *   - CMD      = 命令字
 *   - DATA     = 业务负载 (LEN 字节)
 *   - CHECKSUM = uint16 大端,计算方式:
 *                  sum = ACK + LEN + CMD + DATA[0..LEN-1]
 *                  checksum = ~sum + 1   (取反加一,等价于二补码)
 *                CHECKSUM 自身不参与计算
 *
 * 字段提取边界（边界值检查在 uart_parse.c 中完成）:
 *   - LEN > MAX_DATA   拒绝
 *   - ACK >= 2         拒绝
 *   - CHECKSUM 不匹配  拒绝 (扫描位置仅前进 1 字节,不丢真帧)
 *
 * 业务侧钩子约定（保持现有签名不变）:
 *   void RECEIVE_DATA_A0(uint8 *buffer, uint8 len);
 *   - buffer[0] = CMD
 *   - buffer[1..len-1] = DATA
 *   - len = LEN + 1
 */
#ifndef C02B2_DRV_API_UART_PARSE_H
#define C02B2_DRV_API_UART_PARSE_H

#include <stdint.h>
#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 协议常量 -------------------------------------------------------------- */

/** @brief ICom 帧头低字节 (小端序) */
#define UART_PARSE_HEAD_BYTE0     (0xA5U)
/** @brief ICom 帧头高字节 (小端序) */
#define UART_PARSE_HEAD_BYTE1     (0x5AU)
/** @brief ICom 帧头组合值 (用于按 16 位小端匹配) */
#define UART_PARSE_HEAD_LE        ((uint16)0x5AA5U)

/** @brief 应答位: 数据帧 */
#define UART_PARSE_ACK_DATA       (0U)
/** @brief 应答位: 应答帧 */
#define UART_PARSE_ACK_REPLY      (1U)

/** @brief DATA 字段最大长度 (与 jac 原协议保持一致,LEN 上限) */
#define UART_PARSE_MAX_DATA       (20U)

/** @brief 帧头字节数 */
#define UART_PARSE_HEAD_SIZE      (2U)
/** @brief ACK + LEN + CMD 共 3 字节 */
#define UART_PARSE_FRAME_INFO_SIZE (3U)
/** @brief CHECKSUM 字节数 (uint16 大端) */
#define UART_PARSE_CHECKSUM_SIZE  (2U)

/* 帧间超时:连续两次 byte 时间间隔超过该 tick 数视为帧异常并复位。
 * 由调度器周期性调用 Uart_Parse_Tick() 推进,具体 tick 周期由调用方定义。*/
#define UART_PARSE_FRAME_TIMEOUT_TICKS   (50U)

/* 公开 API ------------------------------------------------------------ */

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
 * @details 推进帧间超时计数器;若在 UART_PARSE_FRAME_TIMEOUT_TICKS
 *          个 tick 内未收到下一字节,清空缓冲。
 *
 * @param[in]  elapsed_ticks  自上次调用以来经过的 tick 数 (>=1)
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
 * @details 默认实现为 weak 空函数。用户可在业务模块的 .c 文件中重新定义
 *          RECEIVE_DATA_A0(去掉 weak)以接收解析后的数据。签名必须保持一致:
 *            void RECEIVE_DATA_A0(uint8 *buffer, uint8 len);
 *
 * @param[in]  buffer  CMD+DATA 指针(指向内部缓冲,仅本次调用期间有效)
 *                     buffer[0] = CMD, buffer[1..len-1] = DATA
 * @param[in]  len     CMD+DATA 总字节数 (= LEN + 1,范围 1..UART_PARSE_MAX_DATA+1)
 *
 * @note    若业务需要长期持有数据,请自行拷贝。
 */
void RECEIVE_DATA_A0(uint8 *buffer, uint8 len);

#ifdef __cplusplus
}
#endif

#endif /* C02B2_DRV_API_UART_PARSE_H */
