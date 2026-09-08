#ifndef  _PRINTF_H_
#define _PRINTF_H_

#define USART1_PRINTF 1
#define USART2_PRINTF 1
#define USART3_PRINTF 0
#define USART6_PRINTF 0
void Usart1Printf(const char *format,...);
void Usart2Printf(const char *format,...);
void Usart3Printf(const char *format,...);
void Usart6Printf(const char *format,...);
#endif
