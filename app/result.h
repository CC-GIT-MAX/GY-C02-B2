/**
 * @file    result.h
 * @brief   Project-wide result / error code definitions
 *
 * All public APIs in this project return c02b2_result_t.
 * Return C02B2_OK (0) on success, non-zero on failure.
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
#ifndef C02B2_RESULT_H
#define C02B2_RESULT_H

#include <stdint.h>
#include <status.h>

typedef int32_t c02b2_result_t;

/* Common --------------------------------------------------------------- */
#define C02B2_OK                        ((c02b2_result_t)0)
#define C02B2_ERR                       ((c02b2_result_t)-1)
#define C02B2_ERR_PARAM                 ((c02b2_result_t)-2)
#define C02B2_ERR_NULL                  ((c02b2_result_t)-3)
#define C02B2_ERR_BUSY                  ((c02b2_result_t)-4)
#define C02B2_ERR_TIMEOUT               ((c02b2_result_t)-5)
#define C02B2_ERR_NOT_READY             ((c02b2_result_t)-6)
#define C02B2_ERR_NOT_SUPPORTED         ((c02b2_result_t)-7)
#define C02B2_ERR_NOT_FOUND             ((c02b2_result_t)-11)
#define C02B2_ERR_INVALID_STATE         ((c02b2_result_t)-8)
#define C02B2_ERR_OVERFLOW              ((c02b2_result_t)-9)
#define C02B2_ERR_UNDERFLOW             ((c02b2_result_t)-10)

/* Power (0x1000) ------------------------------------------------------- */
#define C02B2_ERR_PWR_UV                ((c02b2_result_t)0x1001)
#define C02B2_ERR_PWR_OV                ((c02b2_result_t)0x1002)
#define C02B2_ERR_PWR_LATCH             ((c02b2_result_t)0x1003)

/* CAN / Communication (0x2000) ----------------------------------------- */
#define C02B2_ERR_CAN_BUSOFF            ((c02b2_result_t)0x2001)
#define C02B2_ERR_CAN_TIMEOUT           ((c02b2_result_t)0x2002)
#define C02B2_ERR_CAN_NODATA            ((c02b2_result_t)0x2003)
#define C02B2_ERR_CAN_STUFF             ((c02b2_result_t)0x2004)

/* Storage (0x3000) ----------------------------------------------------- */
#define C02B2_ERR_STORAGE_CRC           ((c02b2_result_t)0x3001)
#define C02B2_ERR_STORAGE_FULL          ((c02b2_result_t)0x3002)
#define C02B2_ERR_STORAGE_NO_NODE       ((c02b2_result_t)0x3003)

/* Sensor (0x4000) ------------------------------------------------------ */
#define C02B2_ERR_SENSOR_OPEN           ((c02b2_result_t)0x4001)
#define C02B2_ERR_SENSOR_SHORT          ((c02b2_result_t)0x4002)
#define C02B2_ERR_SENSOR_RANGE          ((c02b2_result_t)0x4003)

/* Output (0x5000) ------------------------------------------------------ */
#define C02B2_ERR_OUTPUT_LIM            ((c02b2_result_t)0x5001)

/* Helpers -------------------------------------------------------------- */
#define C02B2_SUCCEEDED(r)              ((r) == C02B2_OK)
#define C02B2_FAILED(r)                 ((r) != C02B2_OK)

#endif /* C02B2_RESULT_H */
