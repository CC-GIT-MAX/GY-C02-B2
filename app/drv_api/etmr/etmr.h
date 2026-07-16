/**
 * @file    etmr.h
 * @brief   eTMR peripheral driver API
 * @brief   eTMR 外设驱动接口
 *
 * 该单头文件同时暴露模块级初始化（Etmr_Init()）
 * 与未来可能新增的公共辅助函数。本文件取代了较早的
 * app/drv_api/etmr/etmr_init.c + 缺失 .h 的布局。
 */
#ifndef C02B2_DRV_API_ETMR_H
#define C02B2_DRV_API_ETMR_H

#include "result.h"

/**
 * @brief   Initialize the eTMR peripheral
 * @brief   初始化 eTMR 外设
 *
 * @details 从 board/etmr_config.c 取出 eTMR 配置，
 *          并通过厂商 SDK 驱动应用之。
 *
 * @return  c02b2_result_t    C02B2_OK: Initialization succeeded
 */
c02b2_result_t Etmr_Init(void);

#endif /* C02B2_DRV_API_ETMR_H */
