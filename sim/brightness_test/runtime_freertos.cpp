#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "runtime_freertos.h"
#include <cassert>
#include <cstring>
#include <mutex>

bool fail_mutex_creation = false, fail_task_creation = false;
int task_creations = 0;
static std::mutex state_mutex;
static TaskFunction_t worker = nullptr;
void *xSemaphoreCreateMutex() { return fail_mutex_creation ? nullptr : &state_mutex; }
int xSemaphoreTake(void *handle, unsigned) {
    assert(handle == &state_mutex);
    state_mutex.lock();
    return pdTRUE;
}
int xSemaphoreGive(void *handle) {
    assert(handle == &state_mutex);
    state_mutex.unlock();
    return pdTRUE;
}
int xTaskCreate(TaskFunction_t entry, const char *name, unsigned stack, void *, unsigned, void *) {
    ++task_creations;
    assert(!strcmp(name, "brightness") && stack >= 2048);
    if (fail_task_creation) return 0;
    worker = entry;
    return pdPASS;
}
struct WorkerYield {};
void vTaskDelay(unsigned ticks) { assert(ticks == pdMS_TO_TICKS(100)); throw WorkerYield{}; }
void run_restore_worker_step() {
    assert(worker);
    // Execute the actual task entry until its first delay, without an LVGL
    // event loop or a network stub. Each call represents one worker wakeup.
    try { worker(nullptr); assert(false); } catch (const WorkerYield &) {}
}
