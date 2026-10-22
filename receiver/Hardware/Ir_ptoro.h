#ifndef __IR_PROTO_H
#define __IR_PROTO_H

#include "stm32f10x.h"

//一帧 4 个字节：0xA7 + 温度 + 湿度 + CRC8（多项式 0x07）
//DHT11 读失败时温湿度都填 0xFF，接收端看到就知道是传感器坏了
//发送端和接收端用同一份头文件
#define IR_FRAME_HDR   0xA7
#define IR_ERR_VALUE   0xFF

static __inline uint8_t IR_CRC8(const uint8_t *p, uint8_t len)
{
	uint8_t crc = 0;
	uint8_t i, j;
	for (i = 0; i < len; i++)
	{
		crc ^= p[i];
		for (j = 0; j < 8; j++)
		{
			if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x07);
			else            crc = (uint8_t)(crc << 1);
		}
	}
	return crc;
}

#endif
