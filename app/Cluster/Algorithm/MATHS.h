#include "types.h"
#ifndef __MATHS_H_
#define __MATHS_H_

//-------------------------------------------------------------------
// public functions
//-------------------------------------------------------------------

void FILTER_2B_REFRESH(uint16 * queue,uint16 new,uint8 sum);

uint16 FILTER_2B_AVERAGE(uint16 * queue,uint8 sum);

uint8 FILTER_1B_AVERAGE(uint8 * queue,uint8 new,uint8 sum);

uint8 ASCEND_CHECK(uint8 * p_header,uint8 sum);

uint8 DESCEND_CHECK(uint8 * p_header,uint8 sum);

uint16 CRC16_CHECK(uint8 *data,uint16 start_array_num,uint16 end_array_num);

#endif
