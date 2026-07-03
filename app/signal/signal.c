/**
 * @file    signal.c
 * @brief   Signal bus storage and accessors
 */
#include "signal.h"

static struct {
    int32_t value;
    bool    valid;
} s_signals[SIG_MAX];

lbx_result_t Signal_Set(signal_id_t id, int32_t value)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return LBX_ERR_PARAM;
    }
    s_signals[id].value = value;
    s_signals[id].valid = true;
    return LBX_OK;
}

int32_t Signal_Get(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return 0;
    }
    return s_signals[id].value;
}

bool Signal_IsValid(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return false;
    }
    return s_signals[id].valid;
}

void Signal_Invalidate(signal_id_t id)
{
    if (id <= SIG_INVALID || id >= SIG_MAX) {
        return;
    }
    s_signals[id].valid = false;
}

void Signal_InvalidateAll(void)
{
    for (int i = 0; i < SIG_MAX; i++) {
        s_signals[i].valid = false;
    }
}
