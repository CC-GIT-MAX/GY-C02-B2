/**
 * @file    kv.h
 * @brief   Key-value non-volatile storage
 *
 * Higher-level abstraction over EEPROM / Flash. Keys are 16-bit IDs
 * (allocated centrally to avoid collisions). Values are byte blobs
 * with a maximum size; CRC is computed by the storage layer.
 *
 * Two partitions (active + backup) provide fault tolerance.
 * Wear leveling is the storage layer's responsibility.
 */
#ifndef C02B2_KV_H
#define C02B2_KV_H

#include "types.h"
#include "result.h"

/** @brief  Maximum value length for a single KV entry. */
#define KV_MAX_VALUE_LEN  64u

/**
 * @brief   Initialize the KV storage layer
 * @brief   初始化 KV 存储层
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK  Init succeeded (skeleton returns OK)
 */
c02b2_result_t KV_Init(void);

/**
 * @brief   Read a key from NVM
 * @brief   读取一个键的值
 *
 * @param[in]      key      16-bit key ID
 * @param[out]     buf      Caller-provided buffer
 * @param[in,out]  inout_len [in] buffer size, [out] actual bytes read
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Read OK
 * @retval  C02B2_ERR_NOT_FOUND Skeleton returns this always
 */
c02b2_result_t KV_Get(u16 key, void *buf, u8 *inout_len);

/**
 * @brief   Write a key to the RAM cache (pending Commit)
 * @brief   将一个键写入 RAM 缓存（待 Commit 落盘）
 *
 * @param[in]  key   16-bit key ID
 * @param[in]  buf   Payload
 * @param[in]  len   Payload length (must be <= KV_MAX_VALUE_LEN)
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Value accepted
 * @retval  C02B2_ERR_OVERFLOW  len > KV_MAX_VALUE_LEN
 */
c02b2_result_t KV_Set(u16 key, const void *buf, u8 len);

/**
 * @brief   Mark a key for deletion on next commit
 * @brief   标记一个键在下一次 commit 时删除
 *
 * @param[in]  key  16-bit key ID
 *
 * @return  c02b2_result_t  Always C02B2_OK (skeleton)
 */
c02b2_result_t KV_Delete(u16 key);

/**
 * @brief   Flush pending writes to NVM
 * @brief   将挂起的写操作落盘
 *
 * @return  c02b2_result_t  Always C02B2_OK (skeleton)
 */
c02b2_result_t KV_Commit(void);

/**
 * @brief   Check whether any pending writes are unflushed
 * @brief   检查是否存在未落盘的写操作
 *
 * @return  bool
 * @retval  true   At least one write is pending (skeleton: always false)
 * @retval  false  Cache is clean
 */
bool        KV_IsDirty(void);

#endif /* C02B2_KV_H */
