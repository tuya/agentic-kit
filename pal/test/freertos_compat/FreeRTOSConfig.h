/* Host configuration for syntax checks with official FreeRTOS headers only. */
#define configUSE_PREEMPTION 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configCPU_CLOCK_HZ 48000000UL
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE 128
#define configTOTAL_HEAP_SIZE 4096
#define configMAX_TASK_NAME_LEN 16
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_TIMERS 0
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1

#if TEST_KERNEL_MAJOR == 10
#define configUSE_16_BIT_TICKS (TEST_TICK_BITS == 16)
#else
#if TEST_TICK_BITS == 16
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_16_BITS
#elif TEST_TICK_BITS == 32
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#else
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_64_BITS
#endif
#endif
