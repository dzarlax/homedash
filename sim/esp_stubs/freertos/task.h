#pragma once
#include "FreeRTOS.h"
unsigned ulTaskNotifyTake(int,unsigned);
void vTaskDelay(unsigned);
int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *,int);
void xTaskNotifyGive(TaskHandle_t);
