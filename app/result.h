/**
 * @file    result.h
 * @brief   Project-wide result / error code definitions
 *
 * All public APIs in this project return lbx_result_t.
 * Return LBX_OK (0) on success, non-zero on failure.
 *
 * Ranges:
 *   0x0000xxxx  Generic / common
 *   0x0001xxxx  Power
 *   0x0002xxxx  CAN / communication
 *   0x0003xxxx  Storage (EEPROM / flash)
 *   0x0004xxxx  Sensor / input
 *   0x0005xxxx  Display / output
 *   0x00FFxxxx  Reserved for module-specific extension
 */
#ifndef LBX_RESULT_H
#define LBX_RESULT_H

#include <stdint.h>

typedef int32_t lbx_result_t;

/* Common --------------------------------------------------------------- */
#define LBX_OK                        ((lbx_result_t)0)
#define LBX_ERR                       ((lbx_result_t)-1)
#define LBX_ERR_PARAM                 ((lbx_result_t)-2)
#define LBX_ERR_NULL                  ((lbx_result_t)-3)
#define LBX_ERR_BUSY                  ((lbx_result_t)-4)
#define LBX_ERR_TIMEOUT               ((lbx_result_t)-5)
#define LBX_ERR_NOT_READY             ((lbx_result_t)-6)
#define LBX_ERR_NOT_SUPPORTED         ((lbx_result_t)-7)
#define LBX_ERR_INVALID_STATE         ((lbx_result_t)-8)
#define LBX_ERR_OVERFLOW              ((lbx_result_t)-9)
#define LBX_ERR_UNDERFLOW             ((lbx_result_t)-10)

/* Power (0x1000) ------------------------------------------------------- */
#define LBX_ERR_PWR_UV                ((lbx_result_t)0x1001)
#define LBX_ERR_PWR_OV                ((lbx_result_t)0x1002)
#define LBX_ERR_PWR_LATCH             ((lbx_result_t)0x1003)

/* CAN / Communication (0x2000) ----------------------------------------- */
#define LBX_ERR_CAN_BUSOFF            ((lbx_result_t)0x2001)
#define LBX_ERR_CAN_TIMEOUT           ((lbx_result_t)0x2002)
#define LBX_ERR_CAN_NODATA            ((lbx_result_t)0x2003)
#define LBX_ERR_CAN_STUFF             ((lbx_result_t)0x2004)

/* Storage (0x3000) ----------------------------------------------------- */
#define LBX_ERR_STORAGE_CRC           ((lbx_result_t)0x3001)
#define LBX_ERR_STORAGE_FULL          ((lbx_result_t)0x3002)
#define LBX_ERR_STORAGE_NO_NODE       ((lbx_result_t)0x3003)

/* Sensor (0x4000) ------------------------------------------------------ */
#define LBX_ERR_SENSOR_OPEN           ((lbx_result_t)0x4001)
#define LBX_ERR_SENSOR_SHORT          ((lbx_result_t)0x4002)
#define LBX_ERR_SENSOR_RANGE          ((lbx_result_t)0x4003)

/* Output (0x5000) ------------------------------------------------------ */
#define LBX_ERR_OUTPUT_LIM            ((lbx_result_t)0x5001)

/* Helpers -------------------------------------------------------------- */
#define LBX_SUCCEEDED(r)              ((r) == LBX_OK)
#define LBX_FAILED(r)                 ((r) != LBX_OK)

#endif /* LBX_RESULT_H */
