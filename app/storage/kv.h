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
#ifndef LBX_KV_H
#define LBX_KV_H

#include "types.h"
#include "result.h"

#define KV_MAX_VALUE_LEN  64u

lbx_result_t KV_Init(void);
lbx_result_t KV_Get(u16 key, void *buf, u8 *inout_len);
lbx_result_t KV_Set(u16 key, const void *buf, u8 len);
lbx_result_t KV_Delete(u16 key);
lbx_result_t KV_Commit(void);   /* flush pending writes to NVM */
bool        KV_IsDirty(void);

#endif /* LBX_KV_H */
