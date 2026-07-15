#include "types.h"
#ifndef __MATHS_H_
#define __MATHS_H_

//-------------------------------------------------------------------
// public functions
//-------------------------------------------------------------------

void FILTER_2B_REFRESH(uint16 * queue,uint16 new,uchar8 sum);

uint16 FILTER_2B_AVERAGE(uint16 * queue,uchar8 sum);

uchar8 FILTER_1B_AVERAGE(uchar8 * queue,uchar8 new,uchar8 sum);

uchar8 ASCEND_CHECK(uchar8 * p_header,uchar8 sum);

uchar8 DESCEND_CHECK(uchar8 * p_header,uchar8 sum);

uint16 CRC16_CHECK(uchar8 *data,uint16 start_array_num,uint16 end_array_num);

#endif
