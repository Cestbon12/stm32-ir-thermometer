#include "stm32f10x.h"   // Device header
#include "Delay.h"
#include "OLED.h"
#include "IR_TX.h"
#include "dht11.h"
#include "IR_Proto.h"			//IR_ERR_VALUE

//在线调试的时候改成 0,不然程序一停就复位
#define USE_IWDG        1
//发射周期,一帧大概 70ms,剩下的时间拿去读 DHT11 和刷屏
#define TX_PERIOD_MS    110
//DHT11 两次读取至少要隔 1s
#define DHT_PERIOD_MS   1200
//连续失败这么多次才算传感器坏,偶尔失败一次就继续用上次的值
#define DHT_FAIL_MAX    3

#define MS_TO_TICK(ms)  ((uint16_t)((ms) * 2))	//TIM3 是 0.5ms 一格

static uint8_t g_Temp = 0;		//最近一次读到的温度
static uint8_t g_Humi = 0;		//最近一次读到的湿度
static uint8_t g_Ok = 0;		//1 数据有效,0 传感器故障
static uint8_t g_Fail = 0;		//连续失败次数
static uint8_t g_LastErr = 0;	//1 无应答 2 超时 3 校验错

//TIM3 做成 0.5ms 的自由计数器,用来卡发送周期
static void Tick_Init(void)
{
	TIM_TimeBaseInitTypeDef tb;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
	tb.TIM_Prescaler         = 36000 - 1;			//72MHz / 36000 = 2kHz
	tb.TIM_Period            = 0xFFFF;
	tb.TIM_ClockDivision     = TIM_CKD_DIV1;
	tb.TIM_CounterMode       = TIM_CounterMode_Up;
	tb.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM3, &tb);
	TIM_Cmd(TIM3, ENABLE);
}

static uint16_t Tick_Now(void)
{
	return TIM_GetCounter(TIM3);
}

//独立看门狗,约 2 秒
static void Watchdog_Start(void)
{
#if USE_IWDG
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	IWDG_SetPrescaler(IWDG_Prescaler_64);		//LSI 40kHz / 64 = 625Hz
	IWDG_SetReload(1250);						//1250 / 625 = 2s
	IWDG_ReloadCounter();
	IWDG_Enable();
#endif
}

static void Watchdog_Feed(void)
{
#if USE_IWDG
	IWDG_ReloadCounter();
#endif
}

static void ShowValues(void)
{
	OLED_ShowString(2, 1, "Temp:    C");
	OLED_ShowString(3, 1, "Humi:    %");
	if (g_Ok)
	{
		OLED_ShowNum(2, 7, g_Temp, 2);
		OLED_ShowNum(3, 7, g_Humi, 2);
	}
	else
	{
		OLED_ShowString(2, 7, "--");
		OLED_ShowString(3, 7, "--");
	}
}

static void ShowStatus(void)
{
	if (g_Ok)
	{
		OLED_ShowString(4, 1, "IR Sending OK ");
	}
	else
	{
		OLED_ShowString(4, 1, "DHT11 Err:    ");
		OLED_ShowNum(4, 11, g_LastErr, 1);
	}
}

//读一次 DHT11 并更新状态
static void Dht_Update(void)
{
	DHT11_Data_t d;
	DHT11_Status_t st = DHT11_Read(&d);

	//数据合理性检查,湿度 0~100,温度 0~60
	if (st == DHT11_OK && (d.humi_int > 100 || d.temp_int > 60))
	{
		st = DHT11_ERR_CHECKSUM;
	}

	if (st == DHT11_OK)
	{
		g_Temp = d.temp_int;
		g_Humi = d.humi_int;
		g_Ok = 1;
		g_Fail = 0;
	}
	else
	{
		g_LastErr = (uint8_t)st;
		if (g_Fail < 255) g_Fail++;
		if (g_Fail >= DHT_FAIL_MAX) g_Ok = 0;
	}
	ShowValues();
	ShowStatus();
}

int main(void)
{
	uint16_t tNext;		//下一帧的发送时刻
	uint16_t tDht;		//上一次读 DHT11 的时刻
	uint8_t Blink = 0;

	OLED_Init();
	IR_TX_Init();
	Tick_Init();

	OLED_ShowString(1, 1, "TX DHT11");
	OLED_ShowString(2, 1, "Temp: -- C");
	OLED_ShowString(3, 1, "Humi: -- %");
	OLED_ShowString(4, 1, "Starting...");

	DHT11_Init();			//内部等 1s
	g_Fail = DHT_FAIL_MAX;	//先按故障算,读到数据自动恢复
	Dht_Update();			//先读一次,保证第一帧有内容

	Watchdog_Start();		//初始化都做完了再开狗

	tDht  = Tick_Now();
	tNext = Tick_Now();

	while (1)
	{
		Watchdog_Feed();

		//1. 发一帧,数据有效就发温湿度,坏了就发 0xFF
		if (g_Ok) IR_TX_SendFrame(g_Temp, g_Humi);
		else      IR_TX_SendFrame(IR_ERR_VALUE, IR_ERR_VALUE);

		//2. 到点了就读 DHT11,大概 25ms,塞在两帧中间
		if ((uint16_t)(Tick_Now() - tDht) >= MS_TO_TICK(DHT_PERIOD_MS))
		{
			tDht = Tick_Now();
			Dht_Update();
		}

		//3. 每发一帧闪一下,表示程序还活着
		Blink ^= 1;
		OLED_ShowChar(4, 16, Blink ? '*' : ' ');

		//4. 等下一帧的时刻,落后了就重新对齐
		tNext += MS_TO_TICK(TX_PERIOD_MS);
		if ((int16_t)(Tick_Now() - tNext) >= 0)
		{
			tNext = Tick_Now();
		}
		else
		{
			while ((int16_t)(Tick_Now() - tNext) < 0)
			{
				Watchdog_Feed();
			}
		}
	}
}
