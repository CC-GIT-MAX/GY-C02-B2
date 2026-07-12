/**
 * @file    rti_defer.c
 * @brief   One-shot deferred execution on the RTI tick
 */
#include "rti_defer.h"
#include "rti.h"
#include "result.h"

#include <stddef.h>

/**
 * @brief   Internal slot descriptor
 * @brief   内部槽描述符
 */
typedef struct {
    rti_defer_cb_t  cb;          /**< NULL = slot is free            */
    void           *ctx;         /**< caller-owned context           */
    uint32_t        deadline_ms; /**< RTI tick at which to fire      */
} rti_defer_slot_t;

/** 静态池 —— bare-metal 目标上无 malloc。 */
static rti_defer_slot_t s_slots[RTI_DEFER_SLOTS];

/** 由 RTI_DeferInit() 置 1；防止未初始化即被使用。 */
static uint8_t s_inited = 0u;

/**
 * @brief   Reset the deferred-callback pool
 * @brief   重置延后回调池
 */
void RTI_DeferInit(void)
{
    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        s_slots[i].cb          = NULL;
        s_slots[i].ctx         = NULL;
        s_slots[i].deadline_ms = 0u;
    }
    s_inited = 1u;
}

/**
 * @brief   Number of currently-armed slots
 * @brief   当前已占用槽位数
 */
uint32_t RTI_DeferPending(void)
{
    uint32_t n = 0u;
    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        if (s_slots[i].cb != NULL) { n++; }
    }
    return n;
}

/**
 * @brief   Register a one-shot callback
 * @brief   注册一个一次性回调
 */
c02b2_result_t RTI_Defer(uint32_t delay_ms, rti_defer_cb_t cb, void *ctx)
{
    if (!s_inited) { return C02B2_ERR_NOT_READY; }
    if (cb == NULL) { return C02B2_ERR_PARAM; }

    /* 1. 尝试替换相同 (cb, ctx) 的现有槽位 —— 去抖模式。
     *    时间复杂度 O(n)，但 n=8，开销可忽略。 */
    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        if (s_slots[i].cb == cb && s_slots[i].ctx == ctx) {
            s_slots[i].deadline_ms = RTI_GetTick1ms() + delay_ms;
            return C02B2_OK;
        }
    }

    /* 2. 否则选取第一个空闲槽位。 */
    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        if (s_slots[i].cb == NULL) {
            s_slots[i].cb          = cb;
            s_slots[i].ctx         = ctx;
            s_slots[i].deadline_ms = RTI_GetTick1ms() + delay_ms;
            return C02B2_OK;
        }
    }

    /* 3. 池已耗尽。 */
    return C02B2_ERR_OVERFLOW;
}

/**
 * @brief   Cancel a pending callback
 * @brief   取消尚未触发的回调
 */
c02b2_result_t RTI_DeferCancel(rti_defer_cb_t cb, void *ctx)
{
    if (cb == NULL) { return C02B2_ERR_PARAM; }
    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        if (s_slots[i].cb == cb && s_slots[i].ctx == ctx) {
            s_slots[i].cb          = NULL;
            s_slots[i].ctx         = NULL;
            s_slots[i].deadline_ms = 0u;
            return C02B2_OK;
        }
    }
    return C02B2_ERR_NOT_FOUND;
}

/**
 * @brief   Dispatch any expired callbacks
 * @brief   分发所有已到期的回调
 *
 * @details 两阶段设计允许从回调内重入 RTI_Defer()：
 *          槽位在回调执行前释放，因此回调可在同一槽位
 *          中重新注册。无符号减法处理 tick 环绕。
 */
void RTI_DeferTick(void)
{
    if (!s_inited) { return; }
    const uint32_t now = RTI_GetTick1ms();

    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        rti_defer_cb_t cb  = s_slots[i].cb;
        void           *ctx = s_slots[i].ctx;
        if (cb == NULL) { continue; }
        /* 无符号减法 + cast 成有符号再比较是经典的
         * 单调 deadline 惯用法："已流逝"即差值非负。
         * tick 环绕（每约 49 天）天然得到处理，因为
         * `now` 和 `deadline_ms` 应用相同的环绕后会抵消。 */
        if ((int32_t)(now - s_slots[i].deadline_ms) >= 0) {
            /* 在执行回调之前释放槽位，使得回调体中
             * 重入的 RTI_Defer() 能获得空闲槽位。 */
            s_slots[i].cb          = NULL;
            s_slots[i].ctx         = NULL;
            s_slots[i].deadline_ms = 0u;
            cb(ctx);
        }
    }
}