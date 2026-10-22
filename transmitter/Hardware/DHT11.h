#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f10x.h"

/* 引脚,换板子就改这三行 */
#define DHT11_GPIO_PORT      GPIOB
#define DHT11_GPIO_PIN       GPIO_Pin_0
#define DHT11_GPIO_CLK       RCC_APB2Periph_GPIOB

typedef struct {
    uint8_t humi_int;   /* 湿度整数 %RH */
    uint8_t humi_dec;   /* 湿度小数,DHT11 一直是 0 */
    uint8_t temp_int;   /* 温度整数 ℃ */
    uint8_t temp_dec;   /* 温度小数 */
} DHT11_Data_t;

typedef enum {
    DHT11_OK = 0,
    DHT11_ERR_NO_RESPONSE,  /* 没应答,先查接线和上拉 */
    DHT11_ERR_TIMEOUT,      /* 读到一半超时 */
    DHT11_ERR_CHECKSUM      /* 校验和不对 */
} DHT11_Status_t;

void           DHT11_Init(void);                 /* 内部会等 1s 上电稳定 */
void           DHT11_DelayUs(uint32_t us);
void           DHT11_DelayMs(uint32_t ms);
DHT11_Status_t DHT11_Read(DHT11_Data_t *data);   /* 两次读取间隔要 >= 1s */

#endif /* __DHT11_H */
