#ifndef __TEMPSENSOR_H
#define __TEMPSENSOR_H

void TempSensor_Init(void);
uint16_t TempSensor_GetRaw(void);
int16_t TempSensor_Get(void);

#endif
