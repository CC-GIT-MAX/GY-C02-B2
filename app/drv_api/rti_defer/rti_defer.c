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

/** Static pool - no malloc on the bare-metal target. */
static rti_defer_slot_t s_slots[RTI_DEFER_SLOTS];

/** Set to 1 by RTI_DeferInit(); guards against use-before-init. */
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

    /* 1. Try to replace an existing slot for the same (cb, ctx) - debounce
     *    pattern. This is O(n) but n=8, so cheap. */
    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        if (s_slots[i].cb == cb && s_slots[i].ctx == ctx) {
            s_slots[i].deadline_ms = RTI_GetTick1ms() + delay_ms;
            return C02B2_OK;
        }
    }

    /* 2. Otherwise pick the first free slot. */
    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        if (s_slots[i].cb == NULL) {
            s_slots[i].cb          = cb;
            s_slots[i].ctx         = ctx;
            s_slots[i].deadline_ms = RTI_GetTick1ms() + delay_ms;
            return C02B2_OK;
        }
    }

    /* 3. Pool exhausted. */
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
 * @details Two-pass design to allow re-entrant RTI_Defer() from inside
 *          a callback: the slot is freed BEFORE the callback runs, so
 *          the callback can arm a fresh one in the same slot if it
 *          wants to. Unsigned subtraction handles tick wrap.
 */
void RTI_DeferTick(void)
{
    if (!s_inited) { return; }
    const uint32_t now = RTI_GetTick1ms();

    for (uint32_t i = 0; i < RTI_DEFER_SLOTS; i++) {
        rti_defer_cb_t cb  = s_slots[i].cb;
        void           *ctx = s_slots[i].ctx;
        if (cb == NULL) { continue; }
        /* Unsigned subtraction + cast-to-signed comparison is the
         * classic monotonic-deadline idiom: "elapsed" means the
         * difference is non-negative. Tick wrap (every ~49 days) is
         * handled naturally because the same wrap applied to both
         * `now` and `deadline_ms` cancels out. */
        if ((int32_t)(now - s_slots[i].deadline_ms) >= 0) {
            /* Free the slot BEFORE running the callback so a
             * re-entrant RTI_Defer() in the callback body gets
             * a free slot. */
            s_slots[i].cb          = NULL;
            s_slots[i].ctx         = NULL;
            s_slots[i].deadline_ms = 0u;
            cb(ctx);
        }
    }
}