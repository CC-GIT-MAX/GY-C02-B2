/**
 * @file    uart.h
 * @brief   UART hardware abstraction interface (project-side)
 * @brief   UART 硬件抽象层接口 (项目侧)
 *
 * 本文件只描述 **硬件收发** 接口:
 *   - Uart_Init()         初始化 UART 外设,启动后台接收
 *   - Uart_Send()         非阻塞发送
 *   - Uart_SendBlocking() 阻塞发送
 *   - Uart_RegisterRxByteHandler()  订阅逐字节 RX 事件
 *
 * 与协议解析完全无关。协议解析逻辑见 uart_parse.h / uart_parse.c。
 * 调用方(典型为 uart_parse.c)在初始化时注册一个回调,
 * UART 驱动在每收到一个字节时调用该回调。
 *
 * 当前本项目使用的 UART 实例:
 *   - COMM_uart_config(实例 0): 与外部设备通信,本文件为其封装收发接口
 *   - Printf_uart_config(实例 1): 仅用于 log/printf,不参与本接口
 */
#ifndef C02B2_DRV_API_UART_H
#define C02B2_DRV_API_UART_H

#include <stdint.h>
#include "result.h"
#include "types.h"
#include "callbacks.h"   /* uart_event_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   UART 实例编号(沿用 SDK 的 LINFlexD instance 编号)
 */
typedef enum {
    UART_INSTANCE_COMM   = 0,    /**< 通信 UART (COMM_uart_config) */
    UART_INSTANCE_PRINTF = 1,    /**< 日志 UART (Printf_uart_config) */
} uart_instance_t;

/**
 * @brief   逐字节 RX 事件回调函数原型
 *
 * @details 由上层(典型为 uart_parse.c)通过 Uart_RegisterRxByteHandler()
 *          注册。UART 驱动在 ISR/回调上下文中每收到一个字节即调用一次。
 *          该回调运行于中断上下文,应尽量短小,不做耗时操作。
 *
 * @param[in]  byte   收到的字节
 * @param[in]  ctx    注册时传入的用户上下文
 */
typedef void (*uart_rx_byte_cb_t)(uint8 byte, void *ctx);

/**
 * @brief   初始化 UART 外设(通信实例)
 *
 * @details 从 board/linflexd_uart_config.c 取出 COMM UART 配置,
 *          调用 SDK 驱动完成硬件初始化,并安装 RX/TX/ERR 回调,
 *          启动后台逐字节接收。
 *
 * @return  c02b2_result_t    C02B2_OK: 初始化成功
 *                            C02B2_ERR: SDK 驱动返回错误
 */
c02b2_result_t Uart_Init(void);

/**
 * @brief   非阻塞发送一帧数据
 *
 * @param[in]  buf  发送缓冲区指针
 * @param[in]  len  发送字节数
 * @return  c02B2_result_t    C02B2_OK: 发送已提交
 *                            C02B2_ERR_PARAM: 参数非法
 *                            C02B2_ERR_BUSY: 上一帧未完成
 */
c02b2_result_t Uart_Send(const uint8 *buf, uint32 len);

/**
 * @brief   阻塞发送一帧数据
 *
 * @param[in]  buf          发送缓冲区指针
 * @param[in]  len          发送字节数
 * @param[in]  timeout_ms   超时时间(毫秒),0 表示无限等待
 * @return  c02b2_result_t    C02B2_OK: 发送成功
 *                            C02B2_ERR_TIMEOUT: 超时
 *                            C02B2_ERR_PARAM: 参数非法
 */
c02b2_result_t Uart_SendBlocking(const uint8 *buf, uint32 len, uint32 timeout_ms);

/**
 * @brief   注册逐字节 RX 回调
 *
 * @details 本接口是硬件层与协议解析层之间的唯一桥梁。
 *          协议解析模块在 Uart_Parse_Init() 中调用本函数注册自己,
 *          此后 UART 驱动每收到一个字节即调用 cb。
 *          只允许一个订阅者;重复注册会覆盖前一个订阅者。
 *          该回调在中断上下文中被调用,实现方须自行保证线程/中断安全。
 *
 * @param[in]  cb    回调函数指针,NULL 表示注销
 * @param[in]  ctx   透传给回调的用户上下文,可为 NULL
 */
void Uart_RegisterRxByteHandler(uart_rx_byte_cb_t cb, void *ctx);

/* 内部 SDK 回调(由 SDK LINFlexD UART 驱动调用,业务代码不应直接调用) */
void Uart_RxCallback(void *driverState, uart_event_t event, void *userData);
void Uart_TxCallback(void *driverState, uart_event_t event, void *userData);
void Uart_ErrCallback(void *driverState, uart_event_t event, void *userData);

#ifdef __cplusplus
}
#endif

#endif /* C02B2_DRV_API_UART_H */
