#include "stm32f10x.h"                  // Device header
#include "TempSensor.h"

//用芯片内部温度传感器(ADC1 通道16),不用外接器件
//典型参数:25°C 时 1.43V,斜率 4.3mV/°C
//T = (1.43 - VSENSE) / 0.0043 + 25,VSENSE = 采样值 * 3.3 / 4096

void TempSensor_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);				//ADC 时钟 72/6 = 12MHz,不能超过 14MHz

	ADC_InitTypeDef ADC_InitStructure;
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;		//单次转换
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStructure.ADC_NbrOfChannel = 1;
	ADC_Init(ADC1, &ADC_InitStructure);

	ADC_TempSensorVrefintCmd(ENABLE);						//开内部温度传感器
	ADC_RegularChannelConfig(ADC1, ADC_Channel_16, 1, ADC_SampleTime_239Cycles5);	//采样时间拉长一点更稳

	ADC_Cmd(ADC1, ENABLE);

	ADC_ResetCalibration(ADC1);
	while (ADC_GetResetCalibrationStatus(ADC1) == SET);
	ADC_StartCalibration(ADC1);
	while (ADC_GetCalibrationStatus(ADC1) == SET);

	TempSensor_GetRaw();							//第一次扔掉,让传感器稳定
}

//读原始值,0~4095
uint16_t TempSensor_GetRaw(void)
{
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);
	while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
	return ADC_GetConversionValue(ADC1);
}

//返回值单位 0.1°C,比如 254 就是 25.4°C
int16_t TempSensor_Get(void)
{
	uint16_t Raw = TempSensor_GetRaw();
	uint32_t Vmv = (uint32_t)Raw * 3300 / 4096;		//换算成 mV
	//T(0.1°C) = 250 + (1430 - Vmv) * 100 / 43
	int16_t Temp = 250 + (int16_t)((1430 - (int32_t)Vmv) * 100 / 43);
	return Temp;
}
