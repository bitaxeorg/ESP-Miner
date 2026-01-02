/**
 * @file task.h
 * @brief FreeRTOS task API stub for host-based testing
 */

#ifndef FREERTOS_TASK_H_STUB
#define FREERTOS_TASK_H_STUB

#include "FreeRTOS.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task function type */
typedef void (*TaskFunction_t)(void*);

/* Task creation - stub that doesn't actually create a task */
static inline BaseType_t xTaskCreate(
    TaskFunction_t pvTaskCode,
    const char* const pcName,
    const uint32_t usStackDepth,
    void* const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t* const pxCreatedTask)
{
    (void)pvTaskCode;
    (void)pcName;
    (void)usStackDepth;
    (void)pvParameters;
    (void)uxPriority;
    if (pxCreatedTask) *pxCreatedTask = NULL;
    return pdPASS;
}

/* Task deletion - stub */
static inline void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    (void)xTaskToDelete;
}

/* Get tick count - returns milliseconds since program start */
static inline TickType_t xTaskGetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (TickType_t)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

/* Task delay - actually sleeps on host for testing */
static inline void vTaskDelay(const TickType_t xTicksToDelay)
{
    struct timespec ts;
    uint32_t ms = pdTICKS_TO_MS(xTicksToDelay);
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

/* Task delay until - stub */
static inline BaseType_t xTaskDelayUntil(TickType_t* const pxPreviousWakeTime, const TickType_t xTimeIncrement)
{
    (void)pxPreviousWakeTime;
    vTaskDelay(xTimeIncrement);
    return pdTRUE;
}

/* Get task name - stub */
static inline char* pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    (void)xTaskToQuery;
    return (char*)"stub_task";
}

/* Suspend/Resume - stubs */
static inline void vTaskSuspend(TaskHandle_t xTaskToSuspend) { (void)xTaskToSuspend; }
static inline void vTaskResume(TaskHandle_t xTaskToResume) { (void)xTaskToResume; }

/* Priority manipulation - stubs */
static inline UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask) { (void)xTask; return 1; }
static inline void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority) { (void)xTask; (void)uxNewPriority; }

/* Stack high water mark - stub */
static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask) { (void)xTask; return 1000; }

/* Current task handle - stub */
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return NULL; }

/* Scheduler control - stubs */
static inline void vTaskStartScheduler(void) {}
static inline void vTaskEndScheduler(void) {}
static inline void vTaskSuspendAll(void) {}
static inline BaseType_t xTaskResumeAll(void) { return pdTRUE; }

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_TASK_H_STUB */
