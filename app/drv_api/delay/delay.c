#include "types.h"

#include "YTM32B1MD1.h"

void delay_xms(u16     delay_time) //1ms/unit
{
  u16 i;
  
  for(i=0;i<delay_time;i++)
  {
    _FEED_COP();				// Feed watchdog
    delay_1ms();
  }
}

void delay_1ms(void)
{
  delay_10us(100);
}

void delay_10us(u8 delay_time)		// 10us/unit
{
  u8 i;
  
  for(i=0;i<delay_time;i++)
  {
    delay_1us();
    delay_1us();
    delay_1us();
    delay_1us();
    delay_1us();
    delay_1us();
    delay_1us();
    delay_1us();
    delay_1us();
    delay_1us();
  }
}

void delay_iic(void)
{
  delay_1us();
  delay_1us();
  delay_1us();
}

void delay_1us(void)
{
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");

  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");

  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");

  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");

  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");

  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  __asm("nop");
  
  __asm("nop");
  __asm("nop");
  __asm("nop");
}
