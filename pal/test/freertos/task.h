/* Host-only snapshot API; never placed on a production include path. */
#ifndef INC_TASK_H
#define INC_TASK_H

typedef struct xTIME_OUT {
    BaseType_t xOverflowCount;
    TickType_t xTimeOnEntering;
} TimeOut_t;

void vTaskSetTimeOutState(TimeOut_t *state);

/* Unused by clock tests; refuse task creation rather than start a host worker. */
typedef void *TaskHandle_t;
#define tskIDLE_PRIORITY ((UBaseType_t)0)
static inline BaseType_t xTaskCreate(void (*task)(void *), const char *name,
                                    uint32_t stack, void *arg, UBaseType_t priority,
                                    TaskHandle_t *handle)
{
    (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    return 0;
}
static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
static inline void vTaskSuspend(TaskHandle_t task) { (void)task; }
static inline void vTaskDelete(TaskHandle_t task) { (void)task; }

#endif
