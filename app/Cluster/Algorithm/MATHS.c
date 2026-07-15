//*****************************************************************************
//	CONFIDENTIAL
// 	Copyright (C) 2014 SH.ETC. All rights reserved.
// 	Module			: $ MATHS.c $
// 	Description	: Math Algorithm
// 	Version			: $ Rev: 1.0 $
// 	Last UpDate Time: $Date:: 2021-03-23 16:20:46 $
// 	FOOT NOTE		: TBD
// 	AUTHOR			: SH.ETC.
//*****************************************************************************

#include "MATHS.h"


//*****************************************************************************
// FunName: FILTER_2B_REFRESH
// Desc.	:	FIFO 2-bytes(unsigned short int) queue update
// inputs :	queue:array pointer to update, sum:array length
// outputs: none
//*****************************************************************************
void FILTER_2B_REFRESH(uint16 * queue,uint16 new,uchar8 sum)
{																		// 2-byte moving average filter
	uchar8 i;

	for(i=sum-1;i>0;i--)	queue[i]=queue[i-1];

	queue[0]=new;
}

//*****************************************************************************
// FunName: FILTER_2B_AVERAGE
// Desc.	:	2-bytes(unsigned short int) array average,dont't include the max&min data
// inputs :	queue:array pointer to update, sum:array length
// outputs: average value of array
//*****************************************************************************
uint16 FILTER_2B_AVERAGE(uint16 * queue,uchar8 sum)
{																		// 2-byte moving average filter
	uchar8 i;
	ulong32 temp_sum;
	uint16 temp_min,temp_max;
	
	for(i=1,temp_max=queue[0],temp_min=queue[0];i<sum;i++)
	{
		if(temp_max<queue[i])	temp_max=queue[i];
		if(temp_min>queue[i])	temp_min=queue[i];
	}
	
	for(i=0,temp_sum=0;i<sum;i++)	temp_sum+=queue[i];

	temp_sum=temp_sum-temp_min-temp_max;

	return	((uint16)(temp_sum/(sum-2)));
}

//*****************************************************************************
// FunName: FILTER_1B_AVERAGE
// Desc.	:	1-byte(unsigned char) FIFO queue update and then average,dont't include the max&min data
// inputs :	queue:array pointer to update, sum:array length
// outputs: average value of array
//*****************************************************************************
uchar8 FILTER_1B_AVERAGE(uchar8 * queue,uchar8 new,uchar8 sum)
{																		// 1-byte moving average filter
	uchar8 i;
	uint16 temp_sum;
	uchar8 temp_max,temp_min;

	for(i=sum-1;i>0;i--)	queue[i]=queue[i-1];
	queue[0]=new;
		
	for(i=1,temp_min=queue[0],temp_max=queue[0];i<sum;i++)
	{
		if(temp_max<queue[i])	temp_max=queue[i];
		if(temp_min>queue[i])	temp_min=queue[i];
	}
		
	for(i=0,temp_sum=0;i<sum;i++)	temp_sum+=queue[i];
	
	temp_sum=temp_sum-temp_max-temp_min;
		
	return((uchar8)(temp_sum/(sum-2)));
}

//*****************************************************************************
// FunName: ASCEND_CHECK
// Desc.	:	2-bytes(unsigned short int) array ascend check,use meters input/output eeprom valid check
//					all data !=0xFFFF will be checked, if data value>30000 will retrun false.
// inputs :	queue:array pointer to update, sum:array length
// outputs: if ascend return 1,else 0
//*****************************************************************************
uchar8 ASCEND_CHECK(uchar8 * p_header,uchar8 sum)					// queue ascend check
{
	uint16 temp_data1,temp_data2;
	uchar8 i,equ;

	for(i=0,equ=0;i<=(2*sum-4);i+=2)
	{
		temp_data1=*(p_header+i+1);
		temp_data1=(temp_data1<<8)+*(p_header+i);
		temp_data2=*(p_header+i+3);
		temp_data2=(temp_data2<<8)+*(p_header+i+2);

		if((temp_data1==temp_data2) && (temp_data1<30000))	equ++;

		if(((temp_data1>30000) && (temp_data1<0xffff)) || ((temp_data2>30000) && (temp_data2<0xffff)))
			return 0;
		else if(temp_data1>temp_data2 && temp_data1!=0xffff && temp_data2!=0xffff)
			return 0;
	}

	if(equ>=3)	return 0;
	else return 1;
}

//*****************************************************************************
// FunName: DESCEND_CHECK
// Desc.	:	2-bytes(unsigned short int) array descend check,use meters input/output eeprom valid check
//					all data !=0xFFFF will be checked, if data value>30000 will retrun false.
// inputs :	queue:array pointer to update, sum:array length
// outputs: if descend return 1,else 0
//*****************************************************************************
uchar8 DESCEND_CHECK(uchar8 * p_header,uchar8 sum)				// queue descend check
{
	uint16 temp_data1,temp_data2;
	uchar8 i,equ;

	for(i=0,equ=0;i<=(2*sum-4);i+=2)
	{
		temp_data1=*(p_header+i+1);
		temp_data1=(temp_data1<<8)+*(p_header+i);
		temp_data2=*(p_header+i+3);
		temp_data2=(temp_data2<<8)+*(p_header+i+2);

		if((temp_data1==temp_data2) && (temp_data1<30000))	equ++;

		if(((temp_data1>30000) && (temp_data1<0xffff)) || ((temp_data2>30000) && (temp_data2<0xffff)))
			return 0;
		else if(temp_data1<temp_data2 && temp_data1!=0xffff && temp_data2!=0xffff)
			return 0;
	}

	if(equ>=3)	return 0;
	else return 1;
}

uint16 CRC16_CHECK(uchar8 *data,uint16 start_array_num,uint16 end_array_num)
{
  uint16 j,k,variable;
  uint16 CRCin=0x0000;
  uint16 CRCpoly=0x1021;
  for(k=start_array_num;k<=end_array_num;k++)
  {
    variable=data[k];
  	variable<<=8;  
    CRCin^=variable; 
          
    for(j=0;j<8;j++)  
    {
      if((CRCin&0x8000)!=0) 
      {
        CRCin<<=1;
        CRCin^=CRCpoly;
      } 
      else
      {
        CRCin<<=1;
      }
    }    
  }

  return CRCin;
}
