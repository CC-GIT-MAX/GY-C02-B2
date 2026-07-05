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

/**
 * @brief   Initialize the KV storage layer
 * @brief   初始化 KV 存储层
 *
 * @details Real implementation will:
 *   1. Read partition headers, validate CRC.
 *   2. Select the newer of the two partitions.
 *   3. Build the in-RAM index from the active partition.
 *
 * @return  lbx_result_t  Always LBX_OK (skeleton)
 */
lbx_result_t KV_Init(void)
{
    LOG_I("KV_Init (skeleton)");
    return LBX_OK;
}

/**
 * @brief   Read a key from NVM - skeleton returns NOT_FOUND
 * @brief   读取一个键的值 - 骨架实现返回 NOT_FOUND
 *
 * @param[in]      key      16-bit key ID (unused)
 * @param[out]     buf      Caller-provided buffer (unused)
 * @param[in,out]  inout_len [in] capacity, [out] actual bytes (unused)
 *
 * @return  lbx_result_t  Always LBX_ERR_NOT_FOUND (skeleton)
 */
lbx_result_t KV_Get(u16 key, void *buf, u8 *inout_len)
{
    (void)key; (void)buf; (void)inout_len;
    /* TODO(后续批次): 查表 + CRC 校验. */
    return LBX_ERR_NOT_FOUND;
}

/**
 * @brief   Write a key to the RAM cache
 * @brief   将一个键写入 RAM 缓存
 *
 * @details Skeleton only validates the length. Real implementation
 *          stores the value in a RAM cache and marks the partition
 *          dirty; the next KV_Commit() flushes to flash.
 *
 * @param[in]  key   16-bit key ID
 * @param[in]  buf   Payload
 * @param[in]  len   Payload length
 *
 * @return  lbx_result_t
 * @retval  LBX_OK            Value accepted
 * @retval  LBX_ERR_OVERFLOW  len > KV_MAX_VALUE_LEN
 */
lbx_result_t KV_Set(u16 key, const void *buf, u8 len)
{
    /* Length validation happens before the empty body. */
    if (len > KV_MAX_VALUE_LEN) {
        return LBX_ERR_OVERFLOW;
    }
    (void)key; (void)buf;
    /* TODO: 写入 RAM cache，KV_Commit() 时落盘. */
    return LBX_OK;
}

/**
 * @brief   Mark a key for deletion on next commit
 * @brief   标记一个键在下一次 commit 时删除
 *
 * @param[in]  key  16-bit key ID (unused)
 *
 * @return  lbx_result_t  Always LBX_OK (skeleton)
 */
lbx_result_t KV_Delete(u16 key)
{
    (void)key;
    return LBX_OK;
}

/**
 * @brief   Flush pending writes to NVM
 * @brief   将挂起的写操作落盘
 *
 * @return  lbx_result_t  Always LBX_OK (skeleton)
 */
lbx_result_t KV_Commit(void)
{
    return LBX_OK;
}

/**
 * @brief   Check whether any pending writes are unflushed
 * @brief   检查是否存在未落盘的写操作
 *
 * @return  bool
 * @retval  true   At least one write is pending (skeleton: always false)
 * @retval  false  Cache is clean
 */
bool KV_IsDirty(void)
{
    return false;
}
