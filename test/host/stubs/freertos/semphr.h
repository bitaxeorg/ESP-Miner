/**
 * @file semphr.h
 * @brief FreeRTOS semaphore API stub for host-based testing
 */

#ifndef FREERTOS_SEMPHR_H_STUB
#define FREERTOS_SEMPHR_H_STUB

#include "FreeRTOS.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Semaphore handle wraps pthread mutex for host testing */
typedef struct {
    pthread_mutex_t mutex;
    int initialized;
} SemaphoreStub_t;

/* Create a mutex semaphore - uses pthread mutex on host */
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreStub_t* sem = (SemaphoreStub_t*)malloc(sizeof(SemaphoreStub_t));
    if (sem) {
        pthread_mutex_init(&sem->mutex, NULL);
        sem->initialized = 1;
    }
    return (SemaphoreHandle_t)sem;
}

/* Create a binary semaphore */
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return xSemaphoreCreateMutex();
}

/* Create a counting semaphore */
static inline SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount)
{
    (void)uxMaxCount;
    (void)uxInitialCount;
    return xSemaphoreCreateMutex();
}

/* Take semaphore */
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xBlockTime)
{
    (void)xBlockTime;
    if (!xSemaphore) return pdFAIL;
    SemaphoreStub_t* sem = (SemaphoreStub_t*)xSemaphore;
    if (!sem->initialized) return pdFAIL;
    pthread_mutex_lock(&sem->mutex);
    return pdPASS;
}

/* Give semaphore */
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    if (!xSemaphore) return pdFAIL;
    SemaphoreStub_t* sem = (SemaphoreStub_t*)xSemaphore;
    if (!sem->initialized) return pdFAIL;
    pthread_mutex_unlock(&sem->mutex);
    return pdPASS;
}

/* Delete semaphore */
static inline void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    if (!xSemaphore) return;
    SemaphoreStub_t* sem = (SemaphoreStub_t*)xSemaphore;
    if (sem->initialized) {
        pthread_mutex_destroy(&sem->mutex);
        sem->initialized = 0;
    }
    free(sem);
}

/* Take from ISR - same as regular take on host */
static inline BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore, BaseType_t* pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xSemaphoreTake(xSemaphore, 0);
}

/* Give from ISR - same as regular give on host */
static inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t* pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xSemaphoreGive(xSemaphore);
}

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_SEMPHR_H_STUB */
