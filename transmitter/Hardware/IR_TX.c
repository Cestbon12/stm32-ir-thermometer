#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "IR_Proto.h"

//发射管接 PA0，这个脚同时是 TIM2 的 CH1
#define IR_TX_GPIO    GPIOA
#define IR_TX_PIN     GPIO_Pin_0

//载波来源二选一：
//  定义 IR_TX_PWM_CARRIER：定时器自己发 38kHz，接裸发射管
//  注释掉                ：模块自带 38kHz，DAT 给高电平就发射
#define IR_TX_PWM_CARRIER

//帧参数，接收端必须一致
#define IR_HEADER     0xA5
#define IR_HEADER_ENV 0xA6
#define IR_LEAD_H     9000		//引导码 9ms 载波
#define IR_LEAD_L     4500		//引导码 4.5ms 空闲
#define IR_BIT_H      560		//每位开头 560us 载波
#define IR_BIT0_L     560		//逻辑0 空闲 560us
#define IR_BIT1_L     1690		//逻辑1 空闲 1690us

#ifdef IR_TX_PWM_CARRIER
//72MHz 不分频，1895 个 tick 正好 38kHz
#define IR_PWM_ARR    1894
#define IR_PWM_CCR    947

static void IR_CarrierOn(void)
{
	TIM_SelectOCxM(TIM2, TIM_Channel_1, TIM_OCMode_PWM1);	//这句会把 CC1E 清掉
	TIM_CCxCmd(TIM2, TIM_Channel_1, TIM_CCx_Enable);		//所以得再使能一次
}

static void IR_CarrierOff(void)
{
	TIM_ForcedOC1Config(TIM2, TIM_ForcedAction_InActive);	//强制输出低
}

static void IR_Carrier(uint32_t us)
{
	IR_CarrierOn();
	Delay_us(us);
	IR_CarrierOff();
}

static void IR_Space(uint32_t us)
{
	IR_CarrierOff();
	Delay_us(us);
}
#else
//模块自带 38kHz，DAT 高电平即发射
#define IR_TX_HIGH()  GPIO_SetBits(IR_TX_GPIO, IR_TX_PIN)
#define IR_TX_LOW()   GPIO_ResetBits(IR_TX_GPIO, IR_TX_PIN)

static void IR_Carrier(uint32_t us)
{
	IR_TX_HIGH();
	Delay_us(us);
	IR_TX_LOW();
}

static void IR_Space(uint32_t us)
{
	IR_TX_LOW();
	Delay_us(us);
}
#endif

//一位 = 560us 载波 + 空闲，靠空闲长度区分 0 和 1
static void IR_SendBit(uint8_t Bit)
{
	IR_Carrier(IR_BIT_H);
	if (Bit)
		IR_Space(IR_BIT1_L);
	else
		IR_Space(IR_BIT0_L);
}

//一个字节，低位先发
static void IR_SendByte(uint8_t Data)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		IR_SendBit((Data >> i) & 0x01);
	}
}

void IR_TX_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

#ifdef IR_TX_PWM_CARRIER
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;		//复用推挽，走 TIM2_CH1
	GPIO_InitStructure.GPIO_Pin = IR_TX_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(IR_TX_GPIO, &GPIO_InitStructure);

	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_TimeBaseStructure.TIM_Period = IR_PWM_ARR;
	TIM_TimeBaseStructure.TIM_Prescaler = 0;
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

	TIM_OCInitTypeDef TIM_OCInitStructure;
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse = IR_PWM_CCR;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OC1Init(TIM2, &TIM_OCInitStructure);

	TIM_Cmd(TIM2, ENABLE);
	IR_CarrierOff();					//平时保持低电平
#else
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = IR_TX_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(IR_TX_GPIO, &GPIO_InitStructure);
	IR_TX_LOW();
#endif
}

//0xA5 帧：只发内置温度，单位 0.1°C
void IR_TX_Send(int16_t Temp)
{
	uint8_t Lo = (uint8_t)(Temp & 0xFF);
	uint8_t Hi = (uint8_t)((Temp >> 8) & 0xFF);
	uint8_t Check = Lo ^ Hi;

	IR_Carrier(IR_LEAD_H);
	IR_Space(IR_LEAD_L);

	IR_SendByte(IR_HEADER);
	IR_SendByte(Lo);
	IR_SendByte(Hi);
	IR_SendByte(Check);

	IR_Carrier(IR_BIT_H);				//结束位
}

//0xA6 帧：内置温度 + DHT11 温湿度
void IR_TX_SendEnv(int16_t Temp, uint8_t DhtTemp, uint8_t DhtHumi)
{
	uint8_t Lo = (uint8_t)(Temp & 0xFF);
	uint8_t Hi = (uint8_t)((Temp >> 8) & 0xFF);
	uint8_t Check = Lo ^ Hi ^ DhtTemp ^ DhtHumi;

	IR_Carrier(IR_LEAD_H);
	IR_Space(IR_LEAD_L);

	IR_SendByte(IR_HEADER_ENV);
	IR_SendByte(Lo);
	IR_SendByte(Hi);
	IR_SendByte(DhtTemp);
	IR_SendByte(DhtHumi);
	IR_SendByte(Check);

	IR_Carrier(IR_BIT_H);
}

//0xA7 帧：帧头 + 温度 + 湿度 + CRC8，共 4 字节
void IR_TX_SendFrame(uint8_t Temp, uint8_t Humi)
{
	uint8_t b[4];
	uint8_t i;

	b[0] = IR_FRAME_HDR;
	b[1] = Temp;
	b[2] = Humi;
	b[3] = IR_CRC8(b, 3);

	IR_Carrier(IR_LEAD_H);
	IR_Space(IR_LEAD_L);

	for (i = 0; i < 4; i++)
	{
		IR_SendByte(b[i]);
	}

	IR_Carrier(IR_BIT_H);				//结束位
}
