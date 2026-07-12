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
 * @details 真实实现计划如下：
 *   1. 读出两个分区的头，校验 CRC。
 *   2. 选择两份中较新的那份。
 *   3. 用活跃分区构建内存中的索引。
 */
c02b2_result_t KV_Init(void)
{
    LOG_I("KV_Init (skeleton)");
    return C02B2_OK;
}

/**
 * @brief   Read a key from NVM - skeleton returns NOT_FOUND
 * @brief   读取一个键的值 - 骨架实现返回 NOT_FOUND
 *
 * @param[in]      key      16-bit key ID (unused)
 * @param[out]     buf      Caller-provided buffer (unused)
 * @param[in,out]  inout_len [in] capacity, [out] actual bytes (unused)
 *
 * @return  c02b2_result_t  Always C02B2_ERR_NOT_FOUND (skeleton)
 */
c02b2_result_t KV_Get(u16 key, void *buf, u8 *inout_len)
{
    (void)key; (void)buf; (void)inout_len;
    /* TODO(后续批次): 查表 + CRC 校验. */
    return C02B2_ERR_NOT_FOUND;
}

/**
 * @brief   Write a key to the RAM cache
 * @brief   将一个键写入 RAM 缓存
 *
 * @details 骨架实现只校验长度。真实实现把值写入 RAM 缓存，并标记该
 *          分区为 dirty；下次 KV_Commit() 时落盘。
 */
c02b2_result_t KV_Set(u16 key, const void *buf, u8 len)
{
    /* 长度校验在进入函数体之前完成。*/
    if (len > KV_MAX_VALUE_LEN) {
        return C02B2_ERR_OVERFLOW;
    }
    (void)key; (void)buf;
    /* TODO: 写入 RAM cache，KV_Commit() 时落盘。*/
    return C02B2_OK;
}

/**
 * @brief   Mark a key for deletion on next commit
 * @brief   标记一个键在下一次 commit 时删除
 *
 * @param[in]  key  16-bit key ID (unused)
 *
 * @return  c02b2_result_t  Always C02B2_OK (skeleton)
 */
c02b2_result_t KV_Delete(u16 key)
{
    (void)key;
    return C02B2_OK;
}

/**
 * @brief   Flush pending writes to NVM
 * @brief   将挂起的写操作落盘
 *
 * @return  c02b2_result_t  Always C02B2_OK (skeleton)
 */
c02b2_result_t KV_Commit(void)
{
    return C02B2_OK;
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
