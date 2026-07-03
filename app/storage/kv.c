/**
 * @file    kv.c
 * @brief   KV storage skeleton
 *
 * Uses a single static buffer; real implementation will use two
 * flash partitions with CRC. Business code only calls the public API.
 */
#include "kv.h"

#define LOG_NAME  "KV "
#include "log.h"

lbx_result_t KV_Init(void)
{
    LOG_I("KV_Init (skeleton)");
    return LBX_OK;
}

lbx_result_t KV_Get(u16 key, void *buf, u8 *inout_len)
{
    (void)key; (void)buf; (void)inout_len;
    /* TODO(后续批次): 查表 + CRC 校验 */
    return LBX_ERR_NOT_FOUND;
}

lbx_result_t KV_Set(u16 key, const void *buf, u8 len)
{
    if (len > KV_MAX_VALUE_LEN) {
        return LBX_ERR_OVERFLOW;
    }
    (void)key; (void)buf;
    /* TODO: 写入 RAM cache，KV_Commit() 时落盘 */
    return LBX_OK;
}

lbx_result_t KV_Delete(u16 key)
{
    (void)key;
    return LBX_OK;
}

lbx_result_t KV_Commit(void)
{
    return LBX_OK;
}

bool KV_IsDirty(void)
{
    return false;
}
