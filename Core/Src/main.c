/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "string.h"
#include "math.h"
#include "lcd.h"
#include "printf.h"
#include "w25qxx.h"
#include "cmd.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* USART2 调试控制台接收缓存（中断里仅拷贝，主循环再处理） */
#define CON_BUF_SIZE                16U
static uint8_t           s_con_buf[CON_BUF_SIZE];
static volatile uint16_t s_con_len   = 0U;
static volatile uint8_t  s_con_ready = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

uint8_t buf_tx1[28];
uint8_t buf_tx2[28];
uint8_t buf_rx1[28];
uint8_t buf_rx2[28];

uint8_t spi_tx[128];
uint8_t spi_rx[128];

/* USART2 调试控制台：接收处理（中断回调上下文，仅缓存） */
static void Console_RxHandler(const uint8_t *buf, uint16_t len);

/* USART2 调试控制台：主循环轮询处理（回显 + 按键模拟语音命令） */
static void Console_Task(void);

/* 背光 PWM 亮度设置：duty 0~1000 对应 0~100% */
static void Bsp_SetBacklight(uint16_t duty);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);   /* 开启背光 PWM：PB1 = TIM3_CH4（tim.c） */
	Bsp_SetBacklight(500);                  /* 亮度 50% 起步 */

	/* 语音命令模块初始化（先于挂串口，保证解析状态干净） */
	Cmd_Init();

	/* 挂 USART 空闲中断 + DMA 接收（先挂上，避免初始化耗时期间丢数据） */
	HAL_UARTEx_ReceiveToIdle_DMA(&huart1, buf_rx1, sizeof(buf_rx1));
	HAL_UARTEx_ReceiveToIdle_DMA(&huart2, buf_rx2, sizeof(buf_rx2));

	/* TFT 初始化 + 开机画面 */
	LCD_Init();
	LCD_Clear(LCD_COLOR_BLACK);
	LCD_ShowString(88, 110, "TFT Widget Boot OK", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);

	/* W25Q128 探测（不调 W25Q_Init：Flash 未接好时它会死循环卡死启动） */
	{
	    uint32_t flashId = W25Q_ReadID();

	    if (flashId == W25Q_JEDEC_ID)
	    {
	        Usart2Printf("[FLASH] W25Q128 ready, ID=0x%06lX\r\n", (unsigned long)flashId);
	    }
	    else
	    {
	        Usart2Printf("[FLASH] ID mismatch, got=0x%06lX (expect 0xEF4018)\r\n", (unsigned long)flashId);
	    }
	}

	Usart2Printf("[BOOT] system ready. USART1=ASRPRO voice, USART2=console(1~4 page, b/d brightness)\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Cmd_Task();          /* 处理 ASRPRO 发来的语音命令（校验通过才回调） */
    Console_Task();      /* 处理 USART2 调试键盘输入（1~4 切页 / b、d 调亮度） */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
 * @brief  串口空闲(IDLE)中断 + DMA 接收完成回调
 * @note   帧数据已在 buf_rx1/buf_rx2 中，本函数在中断上下文运行：
 *         只做“解析/缓存 + 重新挂 DMA”，耗时操作交给主循环任务。
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        /* ASRPRO 语音命令：交给 cmd 模块校验缓存（含重新挂接收） */
        if (Size > 0U)
        {
            Cmd_RxHandler(buf_rx1, Size);
        }
        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, buf_rx1, sizeof(buf_rx1));
    }
    else if (huart->Instance == USART2)
    {
        /* 调试控制台：仅拷贝缓存，主循环 Console_Task() 统一处理 */
        if (Size > 0U)
        {
            Console_RxHandler(buf_rx2, Size);
        }
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, buf_rx2, sizeof(buf_rx2));
    }
}

/**
 * @brief  串口错误回调（ORE 溢出等）
 * @note   错误后 DMA 接收会停止，这里统一重新挂上，保证接收不中断。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, buf_rx1, sizeof(buf_rx1));
    }
    else if (huart->Instance == USART2)
    {
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, buf_rx2, sizeof(buf_rx2));
    }
}

/**
 * @brief  背光 PWM 亮度设置
 * @param  duty : 0 ~ 1000，对应 0% ~ 100%（TIM3 计数周期 = 1000，见 tim.c）
 * @retval 无
 */
static void Bsp_SetBacklight(uint16_t duty)
{
    if (duty > 1000U)
    {
        duty = 1000U;
    }
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, duty);
}

/**
 * @brief  USART2 调试控制台接收处理（中断上下文，仅拷贝缓存）
 * @param  buf : 收到的数据
 * @param  len : 数据长度
 * @retval 无
 */
static void Console_RxHandler(const uint8_t *buf, uint16_t len)
{
    uint16_t n = (len > CON_BUF_SIZE) ? CON_BUF_SIZE : len;

    if (n > 0U)
    {
        memcpy(s_con_buf, buf, n);
        s_con_len   = n;
        s_con_ready = 1U;
    }
}

/**
 * @brief  USART2 调试控制台任务（主循环轮询）
 * @note   在 ASRPRO 语音模块还没接上时，用电脑串口助手连 USART2，
 *         敲 1~4 模拟切页、b/d 调背光，即可验证整条命令链路。
 * @retval 无
 */
static void Console_Task(void)
{
    uint8_t  ch    = 0U;
    uint16_t i     = 0U;

    if (s_con_ready == 0U)
    {
        return;
    }
    s_con_ready = 0U;

    for (i = 0U; i < s_con_len; i++)
    {
        ch = s_con_buf[i];

        /* 回显：方便在串口助手里看到敲了什么 */
        HAL_UART_Transmit(&huart2, &ch, 1U, 10U);

        switch (ch)
        {
        case '1':
            Cmd_OnCommand(CMD_PAGE_CLOCK, NULL, 0U);
            break;
        case '2':
            Cmd_OnCommand(CMD_PAGE_WEATHER, NULL, 0U);
            break;
        case '3':
            Cmd_OnCommand(CMD_PAGE_MOOD, NULL, 0U);
            break;
        case '4':
            Cmd_OnCommand(CMD_PAGE_SETTINGS, NULL, 0U);
            break;
        case 'b':   /* 背光 100% */
            Bsp_SetBacklight(1000U);
            Usart2Printf("[CON] backlight 100%%\r\n");
            break;
        case 'd':   /* 背光 10% */
            Bsp_SetBacklight(100U);
            Usart2Printf("[CON] backlight 10%%\r\n");
            break;
        default:
            break;
        }
    }
}

/**
 * @brief  应用层语音命令处理（覆盖 cmd.c 里的弱定义）
 * @note   ASRPRO 语音命令和 USART2 键盘模拟最终都汇聚到这里，
 *         后续 GUI 页面做好后，把 LCD 演示代码换成切页调用即可。
 */
void Cmd_OnCommand(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    switch (cmd)
    {
    case CMD_PAGE_CLOCK:
        LCD_Clear(LCD_COLOR_BLACK);
        LCD_ShowString(116, 110, "CLOCK PAGE", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
        Usart2Printf("[CMD] page -> clock\r\n");
        break;

    case CMD_PAGE_WEATHER:
        LCD_Clear(LCD_COLOR_BLACK);
        LCD_ShowString(108, 110, "WEATHER PAGE", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
        Usart2Printf("[CMD] page -> weather\r\n");
        break;

    case CMD_PAGE_MOOD:
        LCD_Clear(LCD_COLOR_BLACK);
        LCD_ShowString(116, 110, "MOOD PAGE", LCD_COLOR_MAGENTA, LCD_COLOR_BLACK);
        Usart2Printf("[CMD] page -> mood\r\n");
        break;

    case CMD_PAGE_SETTINGS:
        LCD_Clear(LCD_COLOR_BLACK);
        LCD_ShowString(100, 110, "SETTINGS PAGE", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
        Usart2Printf("[CMD] page -> settings\r\n");
        break;

    case CMD_BACKLIGHT:
        if (len >= 1U)
        {
            /* data[0]: 0~100 百分比 -> 0~1000 计数值 */
            Bsp_SetBacklight((uint16_t)data[0] * 10U);
            Usart2Printf("[CMD] backlight -> %u%%\r\n", (unsigned)data[0]);
        }
        break;

    case CMD_PING:
        /* 联调握手：收到 PING 回 ACK（DATA = 0x55） */
        {
            uint8_t ack = 0x55U;

            Cmd_Send(CMD_PING_ACK, &ack, 1U);
            Usart2Printf("[CMD] ping -> ack\r\n");
        }
        break;

    default:
        Usart2Printf("[CMD] unknown cmd=0x%02X\r\n", cmd);
        break;
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
