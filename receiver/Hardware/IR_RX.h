#ifndef __IR_RX_H
#define __IR_RX_H

#include "stm32f10x.h"

void     IR_RX_Init(void);

//取一帧,解码在中断里做完了,这个函数不阻塞
//返回 0 = 没有新数据
//     1 = 收到有效温湿度(*pTemp、*pHumi 有效)
//     2 = 收到传感器故障帧(发送端 DHT11 挂了)
uint8_t  IR_RX_Poll(uint8_t *pTemp, uint8_t *pHumi);

uint32_t IR_RX_GetIdleMs(void);			//离上次收到有效帧过了多少毫秒
uint32_t IR_RX_GetFrameCount(void);		//累计有效帧数(含故障帧),可以拿来算每秒帧数
uint32_t IR_RX_GetBadCount(void);		//累计坏帧数(帧头/CRC/范围不对),用来看干扰大不大
uint16_t IR_RX_Tick(void);				//0.5ms 自由计数器(16 位会回绕),拿来做定时

#endif
