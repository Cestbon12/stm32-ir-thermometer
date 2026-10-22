#include "stm32f10x.h"                  // Device header
#include "IR_RX.h"
#include "Ir_ptoro.h"

//接收头 OUT 接 PA0,双边沿触发 EXTI0
//TIM2 做 1us 自由计数,中断里量相邻两个边沿的间隔,用状态机把 32 位拼出来
//只量边沿间隔、不看电平,所以接收头是高有效还是低有效都不影响
//哪个脉宽不对就立刻复位,等下一个引导码
#define IR_RX_GPIO    GPIOA
#define IR_RX_PIN     GPIO_Pin_0

//脉宽容差,放得比较宽,距离远的时候脉宽会被拉长压短
#define IR_LEAD_MIN   7000		//引导码载波,典型 9ms
#define IR_LEAD_MAX   11000
#define IR_SPACE_MIN  3000		//引导码空闲,典型 4.5ms
#define IR_SPACE_MAX  6000
#define IR_BIT_H_MIN  350		//每位载波,典型 560us
#define IR_BIT_H_MAX  850
#define IR_BIT0_L_MIN 350		//逻辑0 空闲,典型 560us
#define IR_BIT0_L_MAX 850
#define IR_BIT1_L_MIN 1100		//逻辑1 空闲,典型 1690us
#define IR_BIT1_L_MAX 2300

//数据范围
#define IR_TEMP_MAX   60
#define IR_HUMI_MAX   100

//状态机
#define ST_LEAD_MARK   0	//等引导码载波结束
#define ST_LEAD_SPACE  1	//等引导码空闲结束
#define ST_BIT_MARK    2	//等某一位的载波结束
#define ST_BIT_SPACE   3	//等某一位的空闲结束

static uint8_t  s_state = ST_LEAD_MARK;
static uint8_t  s_bit   = 0;
static uint32_t s_data  = 0;
static uint16_t s_last  = 0;

static volatile uint32_t s_frame = 0;		//最近解出来的 32 位
static volatile uint8_t  s_flag  = 0;		//1 表示有新帧等主循环处理

static uint16_t g_LastTick = 0;				//最近一次收到有效帧的 TIM3 计数
static uint32_t g_FrameCnt = 0;
static uint32_t g_BadCnt   = 0;

//TIM2 量脉宽,TIM3 判断多久没收到数据、顺便统计每秒帧数
static void Timer_Init(void)
{
	TIM_TimeBaseInitTypeDef tb;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);

	tb.TIM_Period            = 0xFFFF;
	tb.TIM_Prescaler         = 72 - 1;				//72MHz / 72 = 1MHz,1us 一格
	tb.TIM_ClockDivision     = TIM_CKD_DIV1;
	tb.TIM_CounterMode       = TIM_CounterMode_Up;
	tb.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &tb);
	TIM_Cmd(TIM2, ENABLE);

	tb.TIM_Prescaler         = 36000 - 1;			//2kHz,0.5ms 一格
	TIM_TimeBaseInit(TIM3, &tb);
	TIM_Cmd(TIM3, ENABLE);
}

void IR_RX_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;		//上拉输入,接收头是集电极开路输出
	GPIO_InitStructure.GPIO_Pin = IR_RX_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(IR_RX_GPIO, &GPIO_InitStructure);

	Timer_Init();

	GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
	EXTI_InitStructure.EXTI_Line = EXTI_Line0;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

//每个边沿进来量一次 dt,dt 就是刚结束那一段的宽度
//引导码 9ms -> 4.5ms,然后 32 位,每位是 560us 载波 + 560/1690us 空闲
void EXTI0_IRQHandler(void)
{
	uint16_t now, dt;

	if (EXTI_GetITStatus(EXTI_Line0) == RESET) return;
	EXTI_ClearITPendingBit(EXTI_Line0);

	now = TIM_GetCounter(TIM2);
	dt = (uint16_t)(now - s_last);
	s_last = now;

	switch (s_state)
	{
	case ST_LEAD_MARK:
		if (dt >= IR_LEAD_MIN && dt <= IR_LEAD_MAX)
		{
			s_state = ST_LEAD_SPACE;
			return;
		}
		break;

	case ST_LEAD_SPACE:
		if (dt >= IR_SPACE_MIN && dt <= IR_SPACE_MAX)
		{
			s_state = ST_BIT_MARK;
			s_bit = 0;
			s_data = 0;
			return;
		}
		break;

	case ST_BIT_MARK:
		if (dt >= IR_BIT_H_MIN && dt <= IR_BIT_H_MAX)
		{
			s_state = ST_BIT_SPACE;
			return;
		}
		break;

	case ST_BIT_SPACE:
		if (dt >= IR_BIT0_L_MIN && dt <= IR_BIT0_L_MAX)
		{
			//逻辑0,不用置位
		}
		else if (dt >= IR_BIT1_L_MIN && dt <= IR_BIT1_L_MAX)
		{
			s_data |= (1UL << s_bit);					//逻辑1,低位在前
		}
		else
		{
			break;										//宽度不对,复位
		}

		if (++s_bit >= 32)
		{
			s_frame = s_data;							//一帧收齐,交给主循环校验
			s_flag = 1;
			s_state = ST_LEAD_MARK;
			return;
		}
		s_state = ST_BIT_MARK;
		return;

	default:
		break;
	}

	//走到这里说明宽度不对或者状态乱了,复位重新找引导码
	s_state = ST_LEAD_MARK;
}

//校验并取出一帧
uint8_t IR_RX_Poll(uint8_t *pTemp, uint8_t *pHumi)
{
	uint32_t f;
	uint8_t b[4];

	if (!s_flag) return 0;
	f = s_frame;
	s_flag = 0;

	b[0] = (uint8_t)(f);
	b[1] = (uint8_t)(f >> 8);
	b[2] = (uint8_t)(f >> 16);
	b[3] = (uint8_t)(f >> 24);

	//帧头 + CRC8
	if (b[0] != IR_FRAME_HDR || IR_CRC8(b, 3) != b[3])
	{
		g_BadCnt++;
		return 0;
	}

	//发送端传感器坏了
	if (b[1] == IR_ERR_VALUE && b[2] == IR_ERR_VALUE)
	{
		g_LastTick = TIM_GetCounter(TIM3);
		g_FrameCnt++;
		return 2;
	}

	//范围检查
	if (b[1] > IR_TEMP_MAX || b[2] > IR_HUMI_MAX)
	{
		g_BadCnt++;
		return 0;
	}

	*pTemp = b[1];
	*pHumi = b[2];
	g_LastTick = TIM_GetCounter(TIM3);
	g_FrameCnt++;
	return 1;
}

uint32_t IR_RX_GetIdleMs(void)
{
	return (uint16_t)(TIM_GetCounter(TIM3) - g_LastTick) / 2;	//0.5ms 一格,换算成毫秒
}

uint32_t IR_RX_GetFrameCount(void)
{
	return g_FrameCnt;
}

uint32_t IR_RX_GetBadCount(void)
{
	return g_BadCnt;
}

uint16_t IR_RX_Tick(void)
{
	return TIM_GetCounter(TIM3);
}
