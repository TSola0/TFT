/**
 *******************************************************************************
 * @file    w25qxx.c
 * @brief   W25Q 系列 SPI NOR Flash 驱动实现（当前适配 W25Q128，16MB）
 *
 * 设计说明：
 *   1. 所有接口均为阻塞式（HAL_SPI_Transmit/Receive），简单可靠；
 *      SPI2 未开 DMA 也完全可用，读 1KB 约 0.5ms（18MHz），对挂件绰绰有余；
 *   2. 读写擦接口内部已处理「写使能 → 操作 → 等忙」完整流程；
 *   3. W25Q_Write() 自动处理跨页拆分，调用者只需保证目标区域已擦除；
 *   4. 资源分区表（Res_* 接口）放在 Flash 第 0 个扇区，资源数据从
 *      0x1000（4KB）之后开始布局，由上位机打包工具生成镜像后整体烧入。
 *
 * 上位机打包流程（配合使用）：
 *   - PC 端脚本把图标/字库/动画帧按 ResEntry 格式拼成 w25q128_image.bin；
 *   - 用 SOP8 夹具烧录器把镜像写入芯片，或让 STM32 走 USART2 接收写入；
 *   - 上电后 Res_Init() 读表校验 magic，即可 Res_Read() 按需取资源。
 *
 * @author  梦咕咕 & Shiro
 * @version V1.0
 * @date    2026-09-08
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "w25qxx.h"
#include <string.h>

/* ============================ 内部工具函数 ================================= */

/**
 * @brief 发送 24 位地址（高字节在前）
 * @param addr Flash 内部地址（0 ~ W25Q_FLASH_SIZE-1）
 */
static void W25Q_SendAddr(uint32_t addr)
{
    uint8_t buf[3];

    buf[0] = (uint8_t)(addr >> 16);
    buf[1] = (uint8_t)(addr >> 8);
    buf[2] = (uint8_t)(addr);

    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, buf, 3, 100);
}

/**
 * @brief 读状态寄存器 1
 * @return 状态寄存器值（bit0 = BUSY）
 */
uint8_t W25Q_ReadStatus(void)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS1;
    uint8_t status = 0;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    HAL_SPI_Receive(&W25Q_SPI_HANDLE, &status, 1, 100);
    W25Q_CS_HIGH();

    return status;
}

/**
 * @brief 等待内部擦/写操作完成（轮询 BUSY 位）
 * @param timeoutMs 超时时间（ms），超时后直接返回
 * @note  典型耗时：页编程 0.7ms / 扇区擦除 45ms / 块擦除 150ms / 整片擦除 20s
 */
void W25Q_WaitBusy(uint32_t timeoutMs)
{
    uint32_t count = 0;

    /* 轮询间隔约 1ms：72MHz 下循环体 + HAL 调用已接近该量级 */
    while ((W25Q_ReadStatus() & W25Q_FLAG_BUSY) != 0U)
    {
        if (count >= timeoutMs)
        {
            break;  /* 超时保护：避免死等卡死整个系统 */
        }

        HAL_Delay(1);
        count++;
    }
}

/**
 * @brief 写使能（每次页编程/擦除前必须执行，操作完成后硬件自动失效）
 */
void W25Q_WriteEnable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_CS_HIGH();
}

/**
 * @brief 写禁止
 */
void W25Q_WriteDisable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_DISABLE;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_CS_HIGH();
}

/* ============================ 初始化与识别 ================================= */

/**
 * @brief 读取 JEDEC 器件 ID
 * @return 24 位 ID（W25Q128 = 0xEF4018）
 */
uint32_t W25Q_ReadID(void)
{
    uint8_t cmd = W25Q_CMD_JEDEC_ID;
    uint8_t id[3] = {0, 0, 0};
    uint32_t result = 0;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    HAL_SPI_Receive(&W25Q_SPI_HANDLE, id, 3, 100);
    W25Q_CS_HIGH();

    result = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | (uint32_t)id[2];

    return result;
}

/**
 * @brief 初始化 W25Q128：唤醒 + 校验器件 ID
 * @note  ID 不匹配时进入错误死循环（挂件资源库缺失属于致命错误）；
 *        若希望容错运行，可将 while(1) 改为返回值。
 */
void W25Q_Init(void)
{
    uint32_t id = 0;

    W25Q_WakeUp();
    HAL_Delay(3);   /* 唤醒后 tVSL 最长 3us，留足裕量 */

    id = W25Q_ReadID();

    if (id != W25Q_JEDEC_ID)
    {
        /* ID 校验失败：芯片虚焊 / 接线错误 / 型号不符 */
        while (1)
        {
            /* 可在此处点亮 LED 或串口打印报错 */
        }
    }
}

/* ============================ 读接口 ======================================= */

/**
 * @brief 从 Flash 读取任意长度数据（读命令 0x03）
 * @param addr 起始地址（0 ~ 16777215）
 * @param buf  接收缓冲区（调用者保证容量足够）
 * @param len  读取字节数
 * @note  0x03 命令可以一直连续读（跨越整个芯片容量），无需分块
 */
void W25Q_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint8_t cmd = W25Q_CMD_READ_DATA;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_SendAddr(addr);
    HAL_SPI_Receive(&W25Q_SPI_HANDLE, buf, len, 1000);
    W25Q_CS_HIGH();
}

/**
 * @brief 快速读（读命令 0x0B，比 0x03 支持更高的 SPI 时钟）
 * @param addr 起始地址
 * @param buf  接收缓冲区
 * @param len  读取字节数
 * @note  时序与 W25Q_Read 相同，仅多 1 字节 dummy clock；
 *        SPI 时钟 <= 18MHz 时两者速度无差别，保留此接口备用
 */
void W25Q_FastRead(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint8_t cmd[4];

    cmd[0] = W25Q_CMD_FAST_READ;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)(addr);

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, cmd, 4, 100);
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd[0], 1, 100);   /* dummy byte */
    HAL_SPI_Receive(&W25Q_SPI_HANDLE, buf, len, 1000);
    W25Q_CS_HIGH();
}

/* ============================ 写接口 ======================================= */

/**
 * @brief 页编程（一次最多写入 256 字节）
 * @param addr 起始地址（会自动向上取整到页边界处折返，勿刻意跨页调用）
 * @param buf  数据源
 * @param len  长度（1 ~ 256，若跨页尾部会折返到页首覆盖数据，属未定义行为）
 * @note  内部已含写使能与等忙；目标区域必须已经擦除过（0xFF）
 */
void W25Q_WritePage(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint8_t cmd = W25Q_CMD_PAGE_PROGRAM;

    if (len > W25Q_PAGE_SIZE)
    {
        len = W25Q_PAGE_SIZE;   /* 硬性截断保护 */
    }

    W25Q_WriteEnable();

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_SendAddr(addr);
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, (uint8_t *)buf, len, 100);
    W25Q_CS_HIGH();

    W25Q_WaitBusy(100);     /* 页编程典型 0.7ms，100ms 超时裕量充足 */
}

/**
 * @brief 向 Flash 写入任意长度数据（自动处理跨页拆分）
 * @param addr 起始地址
 * @param buf  数据源
 * @param len  总字节数
 * @note  只写不擦！调用者必须保证目标区域已擦除；
 *        若要「先读-改-写」请在外层配合 W25Q_EraseSector 实现
 */
void W25Q_Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t writeAddr = addr;
    const uint8_t *p = buf;
    uint32_t remain = len;

    while (remain > 0U)
    {
        /* 计算当前地址在页内的剩余空间 */
        uint32_t pageRemain = W25Q_PAGE_SIZE - (writeAddr % W25Q_PAGE_SIZE);
        uint32_t thisLen = (remain < pageRemain) ? remain : pageRemain;

        W25Q_WritePage(writeAddr, p, (uint16_t)thisLen);

        writeAddr += thisLen;
        p += thisLen;
        remain -= thisLen;
    }
}

/* ============================ 擦除接口 ===================================== */

/**
 * @brief 擦除一个 4KB 扇区（包含 addr 的那个扇区）
 * @param addr 扇区内任意地址（内部自动对齐到扇区边界）
 */
void W25Q_EraseSector(uint32_t addr)
{
    uint8_t cmd = W25Q_CMD_SECTOR_ERASE;
    uint32_t sectorAddr = addr & ~(uint32_t)(W25Q_SECTOR_SIZE - 1U);

    W25Q_WriteEnable();

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_SendAddr(sectorAddr);
    W25Q_CS_HIGH();

    W25Q_WaitBusy(500);     /* 扇区擦除典型 45ms，最大 400ms */
}

/**
 * @brief 擦除一个 64KB 块（包含 addr 的那个块）
 * @param addr 块内任意地址（内部自动对齐到块边界）
 */
void W25Q_EraseBlock64K(uint32_t addr)
{
    uint8_t cmd = W25Q_CMD_BLOCK_ERASE_64K;
    uint32_t blockAddr = addr & ~(uint32_t)(W25Q_BLOCK_SIZE - 1U);

    W25Q_WriteEnable();

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_SendAddr(blockAddr);
    W25Q_CS_HIGH();

    W25Q_WaitBusy(1000);    /* 块擦除典型 150ms，最大 2000ms */
}

/**
 * @brief 整片擦除（约 20 秒~80 秒，仅在重新烧录资源库时使用）
 */
void W25Q_EraseChip(void)
{
    uint8_t cmd = W25Q_CMD_CHIP_ERASE;

    W25Q_WriteEnable();

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_CS_HIGH();

    W25Q_WaitBusy(80000);   /* 整片擦除最大 80s，耐心等 */
}

/* ============================ 电源管理 ===================================== */

/**
 * @brief 进入掉电模式（待机电流降至 ~1uA，适合电池供电的挂件）
 */
void W25Q_PowerDown(void)
{
    uint8_t cmd = W25Q_CMD_POWER_DOWN;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_CS_HIGH();

    HAL_Delay(1);   /* tDP 最大 3us */
}

/**
 * @brief 唤醒（退出掉电模式）
 */
void W25Q_WakeUp(void)
{
    uint8_t cmd = W25Q_CMD_RELEASE_POWERDOWN;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&W25Q_SPI_HANDLE, &cmd, 1, 100);
    W25Q_CS_HIGH();

    HAL_Delay(1);   /* tRES1 唤醒时间最大 3us */
}

/* ============================ 资源分区表实现 =============================== */

/** 资源表缓存：上电读入一次，之后查询零开销（128 条 x 12B = 1.5KB RAM） */
static ResEntry s_resTable[RES_TABLE_MAX];
static uint16_t s_resCount = 0;

/**
 * @brief 初始化资源分区表（从 Flash 第 0 扇区读入并校验）
 * @return 1 = 成功；0 = 失败（表不存在 / magic 错误 / 条目超限）
 */
uint8_t Res_Init(void)
{
    uint8_t header[8];
    uint32_t magic = 0;
    uint16_t count = 0;

    W25Q_Read(RES_TABLE_BASE, header, 8);

    memcpy(&magic, header, 4);
    memcpy(&count, &header[4], 2);

    if (magic != RES_TABLE_MAGIC)
    {
        return 0;   /* 表不存在：Flash 还没烧录资源镜像 */
    }

    if ((count == 0U) || (count > RES_TABLE_MAX))
    {
        return 0;   /* 条目数非法 */
    }

    /* 读入全部条目（一次性，之后常驻 RAM） */
    W25Q_Read(RES_TABLE_BASE + 8, (uint8_t *)s_resTable, (uint32_t)count * sizeof(ResEntry));
    s_resCount = count;

    return 1;
}

/**
 * @brief 按资源 ID 查找表项
 * @param id 资源 ID（打包时约定，如 1=天气图标集）
 * @return 表项指针；未找到返回 NULL
 */
const ResEntry *Res_Find(uint16_t id)
{
    uint16_t i;

    for (i = 0; i < s_resCount; i++)
    {
        if (s_resTable[i].id == id)
        {
            return &s_resTable[i];
        }
    }

    return NULL;
}

/**
 * @brief 读取指定资源的（一段）数据
 * @param id     资源 ID
 * @param offset 资源内偏移（相对资源起始处，不是 Flash 绝对地址）
 * @param buf    接收缓冲区
 * @param len    读取字节数
 * @return 1 = 成功；0 = 资源不存在或越界
 * @note  GUI 层显示大图片时可以「分段读 + 分段刷屏」，
 *        复用 lcd.c 的 DMA 分块逻辑，全程不占用大块 RAM
 */
uint8_t Res_Read(uint16_t id, uint32_t offset, uint8_t *buf, uint32_t len)
{
    const ResEntry *entry = Res_Find(id);

    if (entry == NULL)
    {
        return 0;   /* 资源不存在 */
    }

    if ((offset > entry->len) || (len > (entry->len - offset)))
    {
        return 0;   /* 越界保护 */
    }

    W25Q_Read(entry->offset + offset, buf, len);

    return 1;
}
