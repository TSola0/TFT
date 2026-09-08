/**
 *******************************************************************************
 * @file    cmd.h
 * @brief   ASRPRO 语音模块串口通信协议（极简帧格式）
 *
 * 通信对象：ASRPRO 语音识别模块（天问 Block），接 USART1（115200-8-N-1）
 *           TX=PA9 -> ASRPRO_RX / RX=PA10 <- ASRPRO_TX
 *
 * 帧格式（无帧头，靠「空闲线 + 校验和」定界，最短 3 字节）：
 *
 *   ┌──────┬──────┬────────────────────┬──────┐
 *   │ CMD  │ LEN  │ DATA[0]..DATA[n-1] │ SUM  │
 *   ├──────┼──────┼────────────────────┼──────┤
 *   │ 1 字节│ 1 字节│ 0 ~ 16 字节        │ 1 字节│
 *   └──────┴──────┴────────────────────┴──────┘
 *
 *   - CMD : 命令码，见下方 CMD_xxx 宏定义
 *   - LEN : DATA 区长度（无数据命令为 0）
 *   - DATA: 命令附带数据（可为空）
 *   - SUM : 校验和 = (CMD + LEN + 所有 DATA) & 0xFF
 *
 * 设计说明：
 *   1. ASRPRO 每次识别到词条后，把一帧连续发出、帧间保持 >1 字节空闲，
 *      STM32 端用串口空闲(IDLE)+DMA 收整帧，一帧即一次接收事件；
 *   2. 之所以省略 AA/55 帧头：本协议以 USART1 空闲中断定界整帧 + 校验和
 *      验帧，无需帧头同步；命令也更短（切页命令仅 3 字节）；
 *   3. ASRPRO 端（天问 Block）发送例："打开时钟" 词条 → 发 {0x01,0x00,0x01}。
 *
 * 使用流程：
 *   1. main() 初始化后调用 Cmd_Init()；
 *   2. USART1 空闲中断回调里把收到的数据交给 Cmd_RxHandler()（仅解析+置标志）；
 *   3. 主循环调用 Cmd_Task()，校验通过的命令会回调 Cmd_OnCommand()；
 *   4. Cmd_OnCommand 为弱函数，默认空实现，在应用层重新实现即可。
 *
 * @author  梦咕咕 & Shiro
 * @version V1.0
 * @date    2026-09-08
 *******************************************************************************
 */
#ifndef __CMD_H__
#define __CMD_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* ============================ 协议参数配置区 =============================== */

/** DATA 区最大长度（字节）：实际帧最长 = 3 + CMD_DATA_MAX */
#define CMD_DATA_MAX                16U

/** 命令接收处理使用的串口句柄（ASRPRO 所在串口，CubeMX 生成于 usart.c） */
#define CMD_UART_HANDLE             huart1

/** Cmd_Send 阻塞发送超时（ms） */
#define CMD_TX_TIMEOUT              100U

/* ============================ 命令码定义 =================================== */
/* 0x01 ~ 0x0F：页面切换类（DATA 为空） */
#define CMD_PAGE_CLOCK              0x01U   /* 语音："时钟 / 显示时间"      */
#define CMD_PAGE_WEATHER            0x02U   /* 语音："天气"                 */
#define CMD_PAGE_MOOD               0x03U   /* 语音："心情 / 表情"          */
#define CMD_PAGE_SETTINGS           0x04U   /* 语音："设置 / 彩蛋"          */

/* 0x10 ~ 0x1F：控制类 */
#define CMD_BACKLIGHT               0x10U   /* 语音："亮一点/暗一点"
                                                DATA[0] = 0~100 亮度百分比  */

/* 0xF0 ~ 0xFF：联调/系统类 */
#define CMD_PING                    0xF1U   /* ASRPRO 侧联调握手：期望回 ACK */
#define CMD_PING_ACK                0xF2U   /* STM32 应答（DATA 可为 0x55）   */

/* 0x40 ~ 0xEF 留给后续业务扩展（时间同步、天气数据上行等） */

/* ============================ 函数声明 ===================================== */

/** @brief 初始化命令模块（清空解析状态） */
void Cmd_Init(void);

/**
 * @brief  USART1 接收数据处理入口（在 HAL_UARTEx_RxEventCallback 中调用）
 * @param  buf : 本次空闲中断收到的数据指针
 * @param  len : 数据长度
 * @retval 1 = 校验通过并已缓存一帧；0 = 丢弃
 */
uint8_t Cmd_RxHandler(const uint8_t *buf, uint16_t len);

/** @brief 命令任务（主循环周期调用）：有缓存帧时触发 Cmd_OnCommand */
void Cmd_Task(void);

/**
 * @brief  STM32 -> ASRPRO 发送命令
 * @param  cmd  : 命令码
 * @param  data : 附带数据（可为 NULL）
 * @param  len  : 附带数据长度（须 <= CMD_DATA_MAX）
 * @retval 无
 */
void Cmd_Send(uint8_t cmd, const uint8_t *data, uint8_t len);

/**
 * @brief 应用层命令处理回调（弱函数，默认空实现，可覆盖）
 * @param  cmd  : 校验通过的命令码
 * @param  data : 附带数据指针
 * @param  len  : 附带数据长度
 * @retval 无
 */
void Cmd_OnCommand(uint8_t cmd, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* __CMD_H__ */
