/**
 * @file    signal.c
 * @brief   Signal bus storage and accessors
 *
 * Storage: fixed-size array indexed by signal_id_t. Each slot
 * holds the current value plus a `valid` flag. A signal is
 * "set" by writing the value AND marking the slot valid; an
 * invalidated slot returns 0 from Signal_Get() and false from
 * Signal_IsValid().
 */
#include "signal.h"

/* Per-signal storage. `valid` is initially false: callers must
 * call Signal_Set() before they can rely on Signal_Get(). */
static struct {
    int32_t value;
    bool    valid;
} s_signals[SIG_MAX];

/**
 * @brief   Publish a value on the signal bus
 * @brief   在信号总线上发布一个值
 *
 * @param[in]  id     Signal id (see signal_id_t)
 * @param[in]  value  32-bit payload; interpretation depends on signal
 *
 * @return  lbx_result_t
 * @retval  LBX_OK            Value stored and slot marked valid
 * @retval  LBX_ERR_PARAM     id invalid (SIG_INVALID or out of range)
 */
lbx_result_t Signal_Set(signal_id_t id, int32_t value)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return LBX_ERR_PARAM;
    }
    s_signals[id].value = value;
    /* Mark valid so consumers can distinguish fresh data from "never set". */
    s_signals[id].valid = true;
    return LBX_OK;
}

/**
 * @brief   Read the current value of a signal
 * @brief   读取信号的当前值
 *
 * @details Returns 0 for unknown / out-of-range ids and for
 *          invalidated signals. Callers that care about freshness
 *          should use Signal_IsValid().
 *
 * @param[in]  id  Signal id
 *
 * @return  int32_t  Last value set, or 0 if never set / invalid id
 */
int32_t Signal_Get(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return 0;
    }
    return s_signals[id].value;
}

/**
 * @brief   Check whether the signal slot is currently valid
 * @brief   检查信号槽位当前是否有效
 *
 * @param[in]  id  Signal id
 *
 * @return  bool
 * @retval  true   Signal has been set and not invalidated
 * @retval  false  Never set, explicitly invalidated, or invalid id
 */
bool Signal_IsValid(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return false;
    }
    return s_signals[id].valid;
}

/**
 * @brief   Mark a single signal as invalid
 * @brief   将单个信号标记为无效
 *
 * @details Subsequent Signal_Get() returns 0 and Signal_IsValid()
 *          returns false until the signal is Set again.
 *
 * @param[in]  id  Signal id
 */
void Signal_Invalidate(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return;
    }
    /* Just clear the valid bit; keep the value for debugging. */
    s_signals[id].valid = false;
}

/**
 * @brief   Mark every signal as invalid
 * @brief   将所有信号标记为无效
 *
 * @details Used on power-mode transitions / factory reset to force
 *          all consumers to republish their data.
 */
void Signal_InvalidateAll(void)
{
    /* Linear walk is fine: SIG_MAX is small (< 100). */
    for (int i = 0; i < SIG_MAX; i++) {
        s_signals[i].valid = false;
    }
}
