#include "stdio.h"
#include "stdarg.h"
#include "usart.h"
#include "main.h"
#include "printf.h"
uint8_t buf_tx[1280];
#if USART1_PRINTF
void Usart1Printf(const char *format,...)
{
	uint16_t len;
	va_list args;
	va_start(args,format);
	len = vsnprintf((char*)buf_tx,sizeof(buf_tx),(char*)format,args);
	va_end(args);
	HAL_UART_Transmit(&huart1,buf_tx,len,0xff);
}
#endif

#if USART2_PRINTF
void Usart2Printf(const char *format,...)
{
	uint16_t len;
	va_list args;
	va_start(args,format);
	len = vsnprintf((char*)buf_tx,sizeof(buf_tx),(char*)format,args);
	va_end(args);
	HAL_UART_Transmit(&huart2,buf_tx,len,0xff);
}
#endif

#if USART3_PRINTF
void Usart3Printf(const char *format,...)
{
	uint16_t len;
	va_list args;
	va_start(args,format);
	len = vsnprintf((char*)buf_tx,sizeof(buf_tx),(char*)format,args);
	va_end(args);
	HAL_UART_Transmit(&huart3,buf_tx,len,0xff);
}
#endif

#if USART6_PRINTF
void Usart6Printf(const char *format,...)
{
	uint16_t len;
	va_list args;
	va_start(args,format);
	len = vsnprintf((char*)buf_tx,sizeof(buf_tx),(char*)format,args);
	va_end(args);
	HAL_UART_Transmit(&huart6,buf_tx,len,0xff);
}
#endif