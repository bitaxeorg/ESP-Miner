/**
 * @file FreeRTOS.h
 * @brief FreeRTOS stub for host-based testing
 *
 * Provides minimal FreeRTOS types and macros for host compilation.
 */

#ifndef FREERTOS_H_STUB
#define FREERTOS_H_STUB

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic FreeRTOS types */
typedef uint32_t TickType_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* TimerHandle_t;

/* Configuration constants */
#define configTICK_RATE_HZ          1000
#define configMAX_PRIORITIES        25
#define portTICK_PERIOD_MS          (1000 / configTICK_RATE_HZ)
#define portMAX_DELAY               0xFFFFFFFF

/* Boolean values */
#define pdTRUE                      1
#define pdFALSE                     0
#define pdPASS                      pdTRUE
#define pdFAIL                      pdFALSE

/* Tick conversion macros */
#define pdMS_TO_TICKS(xTimeInMs)    ((TickType_t)(((uint64_t)(xTimeInMs) * configTICK_RATE_HZ) / 1000))
#define pdTICKS_TO_MS(xTicks)       ((uint32_t)((uint64_t)(xTicks) * 1000 / configTICK_RATE_HZ))

/* Memory allocation */
#define pvPortMalloc(size)          malloc(size)
#define vPortFree(ptr)              free(ptr)

/* Stack type */
typedef uint32_t StackType_t;

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_H_STUB */
