#include "io_extension.h"
#include <cassert>
#include <cstring>
#include <cstdio>
extern io_extension_obj_t IO_EXTENSION;
static int locked=0, writes=0, fail=0; static uint8_t last[2];
void *xSemaphoreCreateMutex() { return (void*)1; }
int xSemaphoreTake(void *, unsigned) { assert(!locked); locked=1; return 1; }
int xSemaphoreGive(void *) { assert(locked); locked=0; return 1; }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t *data, size_t n, int) {
    assert(locked && n == 2); memcpy(last,data,n); ++writes; return fail ? 9 : ESP_OK;
}
void DEV_I2C_Set_Slave_Addr(i2c_master_dev_handle_t *h, uint8_t) { *h=(void*)1; }
void DEV_I2C_Write_Nbyte(i2c_master_dev_handle_t, uint8_t *data, uint8_t n) {
    assert(n == 2); memcpy(last,data,n); ++writes;
}
void DEV_I2C_Read_Nbyte(i2c_master_dev_handle_t, uint8_t, uint8_t *out, uint8_t) { assert(locked); *out=0; }
uint16_t DEV_I2C_Read_Word(i2c_master_dev_handle_t, uint8_t) { assert(locked); return 0; }
int main() {
    assert(IO_EXTENSION_Pwm_Try_Output(50)==ESP_ERR_INVALID_STATE);
    IO_EXTENSION_Init(); IO_EXTENSION_Output(IO_EXTENSION_IO_2,0);
    uint8_t io=IO_EXTENSION.Last_io_value; assert(last[0]==IO_EXTENSION_IO_OUTPUT_ADDR);
    assert(IO_EXTENSION_Pwm_Try_Output(50)==ESP_OK && last[1]==127);
    assert(IO_EXTENSION_Pwm_Try_Output(60)==ESP_OK && last[1]==153);
    assert(IO_EXTENSION_Pwm_Try_Output(100)==ESP_OK);
    assert(last[0]==IO_EXTENSION_PWM_ADDR && last[1]==(uint8_t)(97*(255/100.0)));
    assert(IO_EXTENSION.Last_io_value==io && !locked);
    fail=1; assert(IO_EXTENSION_Pwm_Try_Output(40)==9 && !locked);
    assert(IO_EXTENSION.Last_io_value==io); puts("brightness driver: serialization, clamp, error and GPIO preservation passed");
}
