/* Host-only link shims. Clock tests do not exercise semaphore operations. */
#ifndef PAL_TEST_SEMPHR_H
#define PAL_TEST_SEMPHR_H

typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return NULL; }
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return NULL; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks)
{
    (void)handle; (void)ticks; return 0;
}
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t handle, TickType_t ticks)
{
    return xSemaphoreTake(handle, ticks);
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{
    (void)handle; return 0;
}
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t handle)
{
    return xSemaphoreGive(handle);
}
static inline void vSemaphoreDelete(SemaphoreHandle_t handle) { (void)handle; }

#endif
