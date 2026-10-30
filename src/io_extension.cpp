#include "io_extension.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

io_extension_obj_t IO_EXTENSION;
static SemaphoreHandle_t s_lock;

static void IO_EXTENSION_IO_Mode(uint8_t pin)
{
    uint8_t data[2] = {IO_EXTENSION_Mode, pin};
    DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, data, 2);
}

void IO_EXTENSION_Init()
{
    s_lock = xSemaphoreCreateMutex();
    DEV_I2C_Set_Slave_Addr(&IO_EXTENSION.addr, IO_EXTENSION_ADDR);
    IO_EXTENSION_IO_Mode(0xff);
    IO_EXTENSION.Last_io_value = 0xFF;
    IO_EXTENSION.Last_od_value = 0xFF;
}

void IO_EXTENSION_Output(uint8_t pin, uint8_t value)
{
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;
    if (value == 1) IO_EXTENSION.Last_io_value |= (1 << pin);
    else IO_EXTENSION.Last_io_value &= (~(1 << pin));
    uint8_t data[2] = {IO_EXTENSION_IO_OUTPUT_ADDR, IO_EXTENSION.Last_io_value};
    DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, data, 2);
    xSemaphoreGive(s_lock);
}

uint8_t IO_EXTENSION_Input(uint8_t pin)
{
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return 0;
    uint8_t value = 0;
    DEV_I2C_Read_Nbyte(IO_EXTENSION.addr, IO_EXTENSION_IO_INPUT_ADDR, &value, 1);
    xSemaphoreGive(s_lock);
    return ((value & (1 << pin)) > 0);
}

esp_err_t IO_EXTENSION_Pwm_Try_Output(uint8_t value)
{
    if (!s_lock || !IO_EXTENSION.addr) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (value > 97) value = 97;
    uint8_t data[2] = {IO_EXTENSION_PWM_ADDR, (uint8_t)(value * (255 / 100.0))};
    esp_err_t err = i2c_master_transmit(IO_EXTENSION.addr, data, sizeof(data), 100);
    xSemaphoreGive(s_lock);
    return err;
}

void IO_EXTENSION_Pwm_Output(uint8_t value)
{
    ESP_ERROR_CHECK(IO_EXTENSION_Pwm_Try_Output(value));
}

uint16_t IO_EXTENSION_Adc_Input()
{
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return 0;
    uint16_t value = DEV_I2C_Read_Word(IO_EXTENSION.addr, IO_EXTENSION_ADC_ADDR);
    xSemaphoreGive(s_lock);
    return value;
}
