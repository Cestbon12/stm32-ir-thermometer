#ifndef __IR_TX_H
#define __IR_TX_H

#include "stm32f10x.h"

void IR_TX_Init(void);
void IR_TX_Send(int16_t Temp);											//0xA5 帧，只有温度
void IR_TX_SendEnv(int16_t Temp, uint8_t DhtTemp, uint8_t DhtHumi);		//0xA6 帧，带 DHT11
void IR_TX_SendFrame(uint8_t Temp, uint8_t Humi);						//0xA7 帧，温湿度 + CRC8

#endif
