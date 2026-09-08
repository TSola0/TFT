/**
 *******************************************************************************
 * @file    w25qxx.h
 * @brief   W25Q 系列 SPI NOR Flash 驱动（当前适配 W25Q128，兼容 W25Q16/32/64）
 *
 * 适配硬件：
 *   - 主控    : STM32F103C8T6（72MHz，SPI2 主机模式）
 *   - 芯片    : W25Q128JV（SOP-8 封装，16MB = 128Mbit）
 *   - 接线    : PB13=SCK  PB14=MISO(DO)  PB15=MOSI(DI)  PB12=CS
 *               WP#/HOLD# 直接上拉到 3.3V（本驱动不使用写保护/暂停功能）
 *
 * 使用说明：
 *   1. 本驱动依赖 CubeMX 生成的 spi.c 中的 hspi2 句柄（需先在 CubeMX 中
 *      使能 SPI2：Full-Duplex Master，Software NSS，波特率建议 Prescaler=4）；
 *   2. 片选脚通过下方宏定义统一管理，改引脚只需改这一处；
 *   3. 调用 W25Q_Init() 完成初始化（含 ID 校验），之后即可使用读写擦接口；
 *   4. 本芯片为 16MB，24 位地址（0x000000 ~ 0xFFFFFF）恰好完整覆盖，
 *      无需 4 字节地址模式；
 *   5. W25Q 系列命令集完全一致，换容量只需改下方容量配置区两个宏。
 *
 * 写操作须知（NOR Flash 特性，务必阅读喵）：
 *   - 写之前必须先擦除，擦除的最小单位是 4KB 扇区；
 *   - 擦除后扇区内所有字节变为 0xFF，写入只能把 1 改成 0；
 *   - 一页（256 字节）内的写操作必须一次完成，跨页要用 W25Q_Write()。
 *
 * @author  梦咕咕 & Shiro
 * @version V1.0
 * @date    2026-09-08
 *******************************************************************************
 */
#ifndef __W25QXX_H__
#define __W25QXX_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "spi.h"

/* ============================ 芯片容量配置区（换型号改这里）================= */

/** Flash 总容量（字节）：W25Q128 = 16MB = 0x1000000 */
#define W25Q_FLASH_SIZE            (16UL * 1024UL * 1024UL)

/** 期望读到的 JEDEC 厂商/器件 ID：W25Q128 = EF 40 18
 *  （W25Q64=EF 40 17  W25Q32=EF 40 16  W25Q16=EF 40 15） */
#define W25Q_JEDEC_ID              0xEF4018UL

/* ============================ 芯片组织参数 ================================= */

/** 页大小：一次编程的最大单位 */
#define W25Q_PAGE_SIZE             256U

/** 扇区大小：擦除的最小单位 */
#define W25Q_SECTOR_SIZE           4096U

/** 块大小：两种块 32KB / 64KB，常用 64KB */
#define W25Q_BLOCK_SIZE            (64U * 1024U)

/** 总扇区数 / 总 64KB 块数 */
#define W25Q_SECTOR_COUNT          (W25Q_FLASH_SIZE / W25Q_SECTOR_SIZE)
#define W25Q_BLOCK_COUNT           (W25Q_FLASH_SIZE / W25Q_BLOCK_SIZE)

/** 状态寄存器 BUSY 位（bit0 = 1 表示内部正在擦写） */
#define W25Q_FLAG_BUSY             0x01U

/* ============================ 指令表（W25Q 全系列通用）===================== */

#define W25Q_CMD_WRITE_ENABLE      0x06U   /* 写使能 */
#define W25Q_CMD_WRITE_DISABLE     0x04U   /* 写禁止 */
#define W25Q_CMD_READ_STATUS1      0x05U   /* 读状态寄存器 1 */
#define W25Q_CMD_WRITE_STATUS1     0x01U   /* 写状态寄存器 1 */
#define W25Q_CMD_READ_DATA         0x03U   /* 读数据（低速，任意长度） */
#define W25Q_CMD_FAST_READ         0x0BU   /* 快速读（最高 104MHz，含 1 字节 dummy） */
#define W25Q_CMD_PAGE_PROGRAM      0x02U   /* 页编程（一次最多 256 字节） */
#define W25Q_CMD_SECTOR_ERASE      0x20U   /* 扇区擦除（4KB） */
#define W25Q_CMD_BLOCK_ERASE_32K   0x52U   /* 块擦除（32KB） */
#define W25Q_CMD_BLOCK_ERASE_64K   0xD8U   /* 块擦除（64KB） */
#define W25Q_CMD_CHIP_ERASE        0xC7U   /* 整片擦除 */
#define W25Q_CMD_JEDEC_ID          0x9FU   /* 读 JEDEC ID（EF 40 xx） */
#define W25Q_CMD_POWER_DOWN        0xB9U   /* 进入掉电模式（待机电流 ~1uA） */
#define W25Q_CMD_RELEASE_POWERDOWN 0xABU   /* 唤醒（也用于读器件 ID） */

/* ============================ 引脚配置区（按需修改）======================== */

/** SPI 句柄：驱动使用的 SPI 外设（CubeMX 生成于 spi.c） */
#define W25Q_SPI_HANDLE            hspi2

/** 片选脚 CS -> 接芯片 CS# */
#define W25Q_CS_PORT               GPIOB
#define W25Q_CS_PIN                GPIO_PIN_12

/* ============================ 底层操作宏（勿随意改动）===================== */

/** 拉低/拉高片选：选中 / 释放 Flash */
#define W25Q_CS_LOW()              HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_RESET)
#define W25Q_CS_HIGH()             HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_SET)

/* ============================ Flash 基础接口 ============================== */

void     W25Q_Init(void);
uint32_t W25Q_ReadID(void);
uint8_t  W25Q_ReadStatus(void);
void     W25Q_WaitBusy(uint32_t timeoutMs);
void     W25Q_WriteEnable(void);
void     W25Q_WriteDisable(void);

void     W25Q_Read(uint32_t addr, uint8_t *buf, uint32_t len);
void     W25Q_FastRead(uint32_t addr, uint8_t *buf, uint32_t len);
void     W25Q_WritePage(uint32_t addr, const uint8_t *buf, uint16_t len);
void     W25Q_Write(uint32_t addr, const uint8_t *buf, uint32_t len);

void     W25Q_EraseSector(uint32_t addr);
void     W25Q_EraseBlock64K(uint32_t addr);
void     W25Q_EraseChip(void);

void     W25Q_PowerDown(void);
void     W25Q_WakeUp(void);

/* ============================ 资源分区表接口 ============================== */

/**
 * 资源分区表：Flash 0 地址处存放一张「资源索引表」，GUI 层按资源 ID 取用，
 * 无需文件系统。表结构（打包工具按此格式生成，详见 .c 文件顶部说明）：
 *
 *   偏移 0x00 : magic  "WRES"（4 字节）
 *   偏移 0x04 : count  资源条目数（uint16）
 *   偏移 0x06 : 保留（2 字节）
 *   偏移 0x08 : 条目数组，每条 12 字节
 *               +0  uint16 id      资源 ID（GUI 层自定义，如 1=天气图标）
 *               +2  uint16 flag    保留（对齐用）
 *               +4  uint32 offset  该资源在 Flash 中的绝对偏移
 *               +8  uint32 len     资源长度（字节）
 */
#define RES_TABLE_MAGIC            0x53455257UL   /* "WRES" 小端 */
#define RES_TABLE_MAX              128U           /* 最多 128 条资源记录 */
#define RES_TABLE_BASE             0U             /* 表固定放在 Flash 头部 */
#define RES_TABLE_AREA_SIZE        4096U          /* 表区占用 1 个扇区 */

/** 资源条目结构（注意：与上位机打包工具的字节布局保持一致） */
typedef struct
{
    uint16_t id;        /* 资源 ID */
    uint16_t flag;      /* 保留 */
    uint32_t offset;    /* Flash 内绝对偏移 */
    uint32_t len;       /* 资源长度 */
} ResEntry;

uint8_t  Res_Init(void);
const ResEntry *Res_Find(uint16_t id);
uint8_t  Res_Read(uint16_t id, uint32_t offset, uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __W25QXX_H__ */
