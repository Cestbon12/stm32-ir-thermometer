#include "dht11.h"

#define DHT11_LOW()    GPIO_ResetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN)
#define DHT11_HIGH()   GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN)
#define DHT11_READ()   GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN)

static uint32_t cyc_per_us = 72;   /* 每微秒多少 CPU 周期,Init 里按主频算 */

/* 这版 core_cm3.h 没有 DWT 的定义,直接写寄存器地址 */
#define DHT11_DEMCR       (*(volatile uint32_t *)0xE000EDFC)
#define DHT11_DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DHT11_DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)

/* 用 DWT 的周期计数器做微秒延时,比 SysTick 准,而且不用改系统节拍 */
static void DWT_Init(void)
{
    SystemCoreClockUpdate();
    cyc_per_us = SystemCoreClock / 1000000U;

    DHT11_DEMCR      |= (1UL << 24);   /* TRCENA,开 DWT */
    DHT11_DWT_CYCCNT  = 0;
    DHT11_DWT_CTRL   |= 1UL;           /* CYCCNTENA,开始计数 */
}

void DHT11_DelayUs(uint32_t us)
{
    uint32_t start = DHT11_DWT_CYCCNT;
    uint32_t ticks = us * cyc_per_us;
    while ((DHT11_DWT_CYCCNT - start) < ticks);
}

void DHT11_DelayMs(uint32_t ms)
{
    while (ms--) {
        DHT11_DelayUs(1000);
    }
}

/* 等引脚变成指定电平,返回等了多少微秒,超时返回 -1 */
static int32_t wait_level(uint8_t level, uint32_t timeout_us)
{
    uint32_t start = DHT11_DWT_CYCCNT;
    uint32_t limit = timeout_us * cyc_per_us;

    while (DHT11_READ() != level) {
        if ((DHT11_DWT_CYCCNT - start) > limit) {
            return -1;
        }
    }
    return (int32_t)((DHT11_DWT_CYCCNT - start) / cyc_per_us);
}

void DHT11_Init(void)
{
    GPIO_InitTypeDef gpio;

    DWT_Init();

    RCC_APB2PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);

    /* 开漏输出:写 1 等于放开总线(靠上拉变高),同时还能读电平 */
    gpio.GPIO_Pin   = DHT11_GPIO_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_GPIO_PORT, &gpio);
    DHT11_HIGH();

    DHT11_DelayMs(1000);   /* 上电后要等 1s 才稳定 */
}

DHT11_Status_t DHT11_Read(DHT11_Data_t *data)
{
    uint8_t buf[5] = {0};
    DHT11_Status_t ret = DHT11_OK;
    int i;

    /* 起始信号:拉低至少 18ms 再放开 */
    DHT11_LOW();
    DHT11_DelayMs(20);
    DHT11_HIGH();

    /* 读的时候不能被打断,整个流程 4~5ms */
    __disable_irq();
    DHT11_DelayUs(30);

    /* 应答:低 80us -> 高 80us -> 开始出数据 */
    if (wait_level(0, 100) < 0) { ret = DHT11_ERR_NO_RESPONSE; goto exit; }
    if (wait_level(1, 100) < 0) { ret = DHT11_ERR_NO_RESPONSE; goto exit; }
    if (wait_level(0, 100) < 0) { ret = DHT11_ERR_NO_RESPONSE; goto exit; }

    /* 40 位,每位都是 50us 低电平开头,高电平 27us 是 0,70us 是 1 */
    for (i = 0; i < 40; i++) {
        if (wait_level(1, 100) < 0) { ret = DHT11_ERR_TIMEOUT; goto exit; }
        DHT11_DelayUs(40);                      /* 40us 后采样刚好在高电平中间 */
        buf[i / 8] <<= 1;
        if (DHT11_READ()) {
            buf[i / 8] |= 1;
        }
        if (wait_level(0, 100) < 0) { ret = DHT11_ERR_TIMEOUT; goto exit; }
    }

exit:
    __enable_irq();
    DHT11_HIGH();   /* 放开总线 */

    if (ret != DHT11_OK) {
        return ret;
    }

    /* 前 4 个字节加起来等于第 5 个 */
    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
        return DHT11_ERR_CHECKSUM;
    }

    data->humi_int = buf[0];
    data->humi_dec = buf[1];
    data->temp_int = buf[2];
    data->temp_dec = buf[3];
    return DHT11_OK;
}
