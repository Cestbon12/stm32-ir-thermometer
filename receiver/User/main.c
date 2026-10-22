#include "stm32f10x.h"   // Device header
#include "OLED.h"
#include "IR_RX.h"

//在线调试的时候改成 0
#define USE_IWDG        1
//超过这个时间没收到有效帧就算断链。发送端 110ms 一帧,差不多等于连丢两帧
#define RX_TIMEOUT_MS   300

typedef enum { LINK_WAIT = 0, LINK_OK, LINK_SENSOR_ERR, LINK_LOST } Link_t;

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

//显示加个缓存,数值没变就不刷屏,省点 I2C 时间
static uint8_t ShownValid = 0xFF;	//0xFF 表示还没画过,保证第一遍一定画
static uint8_t ShownT = 0, ShownH = 0;

static void ShowValues(uint8_t Valid, uint8_t T, uint8_t H)
{
	if (Valid == ShownValid && (!Valid || (T == ShownT && H == ShownH))) return;
	ShownValid = Valid;
	ShownT = T;
	ShownH = H;

	OLED_ShowString(2, 1, "Temp:    C");
	OLED_ShowString(3, 1, "Humi:    %");
	if (Valid)
	{
		OLED_ShowNum(2, 7, T, 2);
		OLED_ShowNum(3, 7, H, 2);
	}
	else
	{
		OLED_ShowString(2, 7, "--");
		OLED_ShowString(3, 7, "--");
	}
}

static void ShowLink(Link_t s)
{
	switch (s)
	{
	case LINK_OK:         OLED_ShowString(4, 1, "Link OK    "); break;
	case LINK_SENSOR_ERR: OLED_ShowString(4, 1, "Sensor Err "); break;
	case LINK_LOST:       OLED_ShowString(4, 1, "No Signal  "); break;
	default:              OLED_ShowString(4, 1, "Waiting... "); break;
	}
}

int main(void)
{
	uint8_t Temp = 0, Humi = 0;
	uint8_t Rx;
	Link_t Link = LINK_WAIT;
	Link_t Shown = LINK_WAIT;

	uint16_t tFps;			//每秒统计一次帧数
	uint32_t LastCnt = 0;
	uint32_t Fps = 0;

	OLED_Init();
	IR_RX_Init();

	OLED_ShowString(1, 1, "RX DHT11");
	ShowValues(0, 0, 0);
	ShowLink(LINK_WAIT);
	OLED_ShowString(4, 12, "F:");
	OLED_ShowNum(4, 14, 0, 2);

	Watchdog_Start();
	tFps = IR_RX_Tick();

	while (1)
	{
		Watchdog_Feed();

		Rx = IR_RX_Poll(&Temp, &Humi);
		if (Rx == 1)						//有效温湿度
		{
			Link = LINK_OK;
			ShowValues(1, Temp, Humi);
		}
		else if (Rx == 2)					//传感器故障帧
		{
			Link = LINK_SENSOR_ERR;
			ShowValues(0, 0, 0);
		}
		else if ((Link == LINK_OK || Link == LINK_SENSOR_ERR) && IR_RX_GetIdleMs() > RX_TIMEOUT_MS)
		{
			Link = LINK_LOST;				//超时,马上清屏
			ShowValues(0, 0, 0);
		}

		if (Link != Shown)
		{
			ShowLink(Link);
			Shown = Link;
		}

		//每 1 秒统计一次每秒收到几帧,信号好的时候大概 9 帧/秒
		if ((uint16_t)(IR_RX_Tick() - tFps) >= 2000)	//2000 格 * 0.5ms = 1s
		{
			tFps += 2000;
			Fps = IR_RX_GetFrameCount() - LastCnt;
			LastCnt = IR_RX_GetFrameCount();
			OLED_ShowNum(4, 14, Fps, 2);
		}
	}
}
