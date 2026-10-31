#pragma once
using TaskFunction_t = void (*)(void *);
int xTaskCreate(TaskFunction_t, const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(unsigned);
