/**
 *******************************************************************************
 * @file    cmd.c
 * @brief   ASRPRO 语音模块串口通信协议实现（极简帧格式）
 *
 * 帧格式：[CMD][LEN][DATA...][SUM]，详见 cmd.h 顶部说明。
 * 收帧策略：USART1 空闲(IDLE)中断天然按"一次语音识别 = 一帧"把数据切好，
 *           本文件只做"长度 + 校验和"验证，验证通过置就绪标志，
 *           真正的业务处理放在 Cmd_Task() 里（主循环上下文，避免在
 *           中断里做耗时的界面刷新）。
 *
 * @author  梦咕咕 & Shiro
 * @version V1.0
 * @date    2026-09-08
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "cmd.h"
#include "usart.h"
#include <string.h>

/* ============================ 私有变量 ===================================== */

/** 最近一帧校验通过的命令码 */
static volatile uint8_t s_rx_cmd = 0U;

/** 最近一帧附带数据（长度 <= CMD_DATA_MAX） */
static uint8_t s_rx_data[CMD_DATA_MAX];

/** 最近一帧附带数据长度 */
static volatile uint8_t s_rx_len = 0U;

/** 帧就绪标志：1 = 有一帧待 Cmd_Task 处理 */
static volatile uint8_t s_rx_ready = 0U;

/* ============================ 函数实现 ===================================== */

/**
 * @brief  初始化命令模块
 * @retval 无
 */
void Cmd_Init(void)
{
    s_rx_cmd   = 0U;
    s_rx_len   = 0U;
    s_rx_ready = 0U;
    memset(s_rx_data, 0, sizeof(s_rx_data));
}

/**
 * @brief  USART1 接收数据处理入口（HAL_UARTEx_RxEventCallback 中调用）
 * @param  buf : 本次接收到的数据指针
 * @param  len : 数据长度
 * @retval 1 = 校验通过并已缓存；0 = 无效帧被丢弃
 */
uint8_t Cmd_RxHandler(const uint8_t *buf, uint16_t len)
{
    uint8_t  dlen  = 0U;
    uint8_t  sum   = 0U;
    uint16_t i     = 0U;

    /* 参数合法性：最短 3 字节（CMD+LEN+SUM） */
    if ((buf == NULL) || (len < 3U))
    {
        return 0U;
    }

    dlen = buf[1];

    /* 长度合法性：DATA 不超过上限，且总长必须严格等于 3 + LEN */
    if ((dlen > CMD_DATA_MAX) || ((uint16_t)dlen + 3U != len))
    {
        return 0U;
    }

    /* 校验和：SUM = (CMD + LEN + 所有 DATA) & 0xFF */
    sum = (uint8_t)(buf[0] + buf[1]);
    for (i = 0U; i < (uint16_t)dlen; i++)
    {
        sum = (uint8_t)(sum + buf[2U + i]);
    }
    if (sum != buf[len - 1U])
    {
        return 0U;   /* 校验失败：线路干扰或帧不对齐，整帧丢弃 */
    }

    /* 校验通过：缓存帧内容，等主循环处理 */
    s_rx_cmd = buf[0];
    s_rx_len = dlen;
    if (dlen > 0U)
    {
        memcpy(s_rx_data, &buf[2U], dlen);
    }
    s_rx_ready = 1U;

    return 1U;
}

/**
 * @brief  命令任务（主循环周期调用）
 * @retval 无
 * @note   有缓存帧时回调 Cmd_OnCommand()，随后自动清就绪标志；
 *         若应用层新一帧未到，本函数为纯检查、耗时几乎为零。
 */
void Cmd_Task(void)
{
    uint8_t cmd = 0U;
    uint8_t len = 0U;

    if (s_rx_ready == 0U)
    {
        return;
    }

    /* 取出帧内容并清标志（防止回调中再次触发） */
    cmd = s_rx_cmd;
    len = s_rx_len;
    s_rx_ready = 0U;

    /* 交给应用层处理（主循环上下文，可放心做界面操作） */
    Cmd_OnCommand(cmd, s_rx_data, len);
}

/**
 * @brief  STM32 -> ASRPRO 发送命令
 * @param  cmd  : 命令码
 * @param  data : 附带数据（可为 NULL）
 * @param  len  : 附带数据长度（须 <= CMD_DATA_MAX）
 * @retval 无
 */
void Cmd_Send(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    static uint8_t frame[CMD_DATA_MAX + 3U];   /* 帧缓冲（static：防栈回收） */
    uint8_t  sum = 0U;
    uint8_t  i   = 0U;

    if (len > CMD_DATA_MAX)
    {
        len = CMD_DATA_MAX;                    /* 超长截断，保证帧结构不坏 */
    }

    frame[0] = cmd;
    frame[1] = len;
    sum = (uint8_t)(cmd + len);

    if ((len > 0U) && (data != NULL))
    {
        for (i = 0U; i < len; i++)
        {
            frame[2U + i] = data[i];
            sum = (uint8_t)(sum + data[i]);
        }
    }
    frame[2U + len] = sum;                     /* 追加校验和 */

    HAL_UART_Transmit(&CMD_UART_HANDLE, frame, (uint16_t)len + 3U, CMD_TX_TIMEOUT);
}

/**
 * @brief  应用层命令处理回调（弱定义默认空实现）
 * @note   在应用层（如 main.c USER CODE 区）重新实现同签名函数即可生效
 */
__weak void Cmd_OnCommand(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    /* 默认不做任何处理 */
    (void)cmd;
    (void)data;
    (void)len;
}
