/**
 * @file    rti.h
 * @brief   Real-time interrupt tick generator and period flags
 *
 * Hooked to LPTMR or SysTick at 1 ms resolution. Modules register a
 * period (5 / 10 / 20 / 50 / 100 / 250 / 500 / 1000 ms) and check
 * RTI_IsElapsed(period) each call. State is owned by the module
 * (no global flag variables exposed).
 */
#ifndef LBX_RTI_H
#define LBX_RTI_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   Supported RTI sub-periods
 * @brief   支持的 RTI 子周期
 */
typedef enum {
    RTI_5MS    = 5,
    RTI_10MS   = 10,
    RTI_20MS   = 20,
    RTI_50MS   = 50,
    RTI_100MS  = 100,
    RTI_250MS  = 250,
    RTI_500MS  = 500,
    RTI_1000MS = 1000,
} rti_period_t;

/**
 * @brief   Initialize the 1 ms RTI counter
 * @brief   初始化 1ms RTI 计数器
 *
 * @note    Hardware LPTMR/SysTick configuration is performed by
 *          board/board_init.c; this function only resets the tick.
 */
void     RTI_Init(void);

/**
 * @brief   1 kHz tick callback invoked from the LPTMR ISR
 * @brief   LPTMR ISR 调用的 1kHz tick 回调
 *
 * @details Must be called from the LPTMR ISR (1 kHz). The ISR is
 *          also responsible for clearing the LPTMR interrupt flag
 *          and (optionally) feeding the watchdog.
 *
 * @note    Runs in ISR context; do not block.
 */
void     RTI_OnTick1ms(void);

/**
 * @brief   Get the current 1 ms tick count
 * @brief   获取当前 1ms tick 计数
 *
 * @return  uint32_t  Monotonic 1 ms tick (wraps after ~49 days)
 */
uint32_t RTI_GetTick1ms(void);

/**
 * @brief   Check whether the requested period has elapsed since the last call
 * @brief   检查自上次调用以来指定周期是否已到
 *
 * @details Each caller maintains its own per-slot last-run tick.
 *          When the difference reaches `period`, the function
 *          updates the slot and returns true.
 *
 * @param[in]  period  One of RTI_5MS..RTI_1000MS
 *
 * @return  bool
 * @retval  true   Period elapsed (caller should run its sub-task)
 * @retval  false  Not yet elapsed (caller should skip)
 *
 * @note    The per-slot state is shared across callers, so this
 *          helper is best for "I am the only one needing this
 *          period" scenarios. Use RTI_GetTick1ms() for explicit
 *          timestamps when more than one caller shares a period.
 */
bool     RTI_IsElapsed(rti_period_t period);

/**
 * @brief   Detect the first call after power-on or RTI_Init
 * @brief   检测上电或 RTI_Init 之后的第一次调用
 *
 * @details Useful for one-shot initialization inside a tick body
 *          without needing a separate `init_done` flag.
 *
 * @return  bool
 * @retval  true   First call (run the init branch)
 * @retval  false  Subsequent calls
 */
bool     RTI_IsFirstCall(void);

#endif /* LBX_RTI_H */
