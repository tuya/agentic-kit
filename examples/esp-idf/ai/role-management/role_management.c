#include "role_management.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#define ROLE_DEMO_STOP ((role_demo_operation_t)-1)
static const char *TAG = "role-demo";
static QueueHandle_t jobs;
static SemaphoreHandle_t stopped;
static tuya_biz_client_t *biz;
static role_demo_done_fn done;
static void *done_data;

static int show_bound(iot_atop_rejection_t *err)
{
    tuya_ai_role_t role;
    int rc = tuya_ai_role_get_bound(biz, &role, err);
    if (!rc) ESP_LOGI(TAG, "bound: id=%s name=%s type=%d", role.role_id, role.name, role.type);
    return rc;
}
static void worker(void *arg)
{
    (void)arg; role_demo_job_t job;
    while (xQueueReceive(jobs, &job, portMAX_DELAY) == pdTRUE) {
        if (job.operation == ROLE_DEMO_STOP) break;
        iot_atop_rejection_t err = {0}; int rc = OPRT_INVALID_PARAMETER;
        bool changed = false;
        if (job.operation == ROLE_DEMO_LOAD) {
            tuya_ai_role_list_t templates = {0}, customs = {0};
            rc = tuya_ai_role_list_templates(biz, &templates, &err);
            if (!rc) rc = tuya_ai_role_list_custom(biz, job.page_no, job.page_size, &customs, &err);
            if (!rc) {
                for (size_t i = 0; i < templates.count; ++i)
                    ESP_LOGI(TAG, "template: %s %s", templates.items[i].role_id, templates.items[i].name);
                for (size_t i = 0; i < customs.count; ++i)
                    ESP_LOGI(TAG, "custom: %s %s", customs.items[i].role_id, customs.items[i].name);
                rc = show_bound(&err);
            }
            tuya_ai_role_list_free(biz, &templates); tuya_ai_role_list_free(biz, &customs);
        } else if (job.operation == ROLE_DEMO_BIND) {
            rc = tuya_ai_role_bind(biz, job.type, job.role_id, &err); changed = !rc;
        } else if (job.operation == ROLE_DEMO_EDIT) {
            tuya_ai_role_t role = { .type = job.type };
            memcpy(role.role_id, job.role_id, sizeof(role.role_id));
            tuya_ai_role_edit_t edit = { .name = job.set_name ? job.name : NULL,
                .lang_code = job.set_lang ? job.lang : NULL,
                .timbre_id = job.set_timbre ? job.timbre : NULL,
                .set_speed = job.set_speed, .speed = job.speed };
            char id[TUYA_AI_ROLE_ID_LEN];
            rc = tuya_ai_role_apply_edit(biz, &role, &edit, id, &err);
            if (!rc) ESP_LOGI(TAG, "saved role id=%s", id);
            changed = !rc;
        } else if (job.operation == ROLE_DEMO_DELETE && job.type == TUYA_AI_ROLE_CUSTOM) {
            tuya_ai_role_t bound;
            rc = tuya_ai_role_get_bound(biz, &bound, &err);
            if (!rc && !strcmp(bound.role_id, job.role_id)) rc = OPRT_INVALID_PARAMETER;
            else if (!rc) { rc = tuya_ai_role_delete_custom(biz, job.role_id, &err); changed = !rc; }
        } else if (job.operation == ROLE_DEMO_LANGUAGES) {
            tuya_ai_language_list_t list = {0};
            rc = tuya_ai_role_list_languages(biz, &list, &err);
            if (!rc) for (size_t i = 0; i < list.count; ++i)
                ESP_LOGI(TAG, "language: %s %s", list.items[i].code, list.items[i].name);
            tuya_ai_language_list_free(biz, &list);
        } else if (job.operation == ROLE_DEMO_TIMBRES) {
            tuya_ai_timbre_list_t list = {0};
            rc = tuya_ai_role_list_timbres(biz, job.lang[0] ? job.lang : NULL,
                                          job.page_no, job.page_size, &list, &err);
            if (!rc) for (size_t i = 0; i < list.count; ++i)
                ESP_LOGI(TAG, "timbre: %s %s", list.items[i].id, list.items[i].name);
            tuya_ai_timbre_list_free(biz, &list);
        }
        if (changed) {
            ESP_LOGI(TAG, "change accepted; refreshing bound role");
            rc = show_bound(&err);
            if (rc) ESP_LOGW(TAG, "change accepted but refresh failed; query before retrying a write");
        }
        ESP_LOGI(TAG, "operation=%d rc=%d cloud=%s %s", job.operation, rc, err.code, err.msg);
        if (done) done(job.operation, rc, changed, &err, done_data);
    }
    tuya_biz_client_destroy(biz); biz = NULL;
    xSemaphoreGive(stopped);
    vTaskDelete(NULL);
}

bool role_management_start(iot_client_t *iot, role_demo_done_fn cb, void *ud)
{
    if (jobs) return false;
    if (tuya_biz_client_create(iot, "zh", &biz)) return false;
    jobs = xQueueCreate(4, sizeof(role_demo_job_t));
    stopped = xSemaphoreCreateBinary(); done = cb; done_data = ud;
    if (jobs && stopped && xTaskCreate(worker, "role-demo", 16384, NULL, 4, NULL) == pdPASS) return true;
    if (jobs) vQueueDelete(jobs);
    if (stopped) vSemaphoreDelete(stopped);
    jobs = NULL; stopped = NULL;
    tuya_biz_client_destroy(biz); biz = NULL;
    return false;
}
bool role_management_submit(const role_demo_job_t *job)
{
    if (!jobs || !job || job->operation < ROLE_DEMO_LOAD || job->operation > ROLE_DEMO_TIMBRES)
        return false;
    /* All fixed arrays must be terminated before a worker may pass them on. */
    if (!memchr(job->role_id, 0, sizeof(job->role_id)) ||
        !memchr(job->name, 0, sizeof(job->name)) || !memchr(job->lang, 0, sizeof(job->lang)) ||
        !memchr(job->timbre, 0, sizeof(job->timbre))) return false;
    return xQueueSend(jobs, job, 0) == pdTRUE;
}
void role_management_stop(void)
{
    if (!jobs) return;
    role_demo_job_t job = { .operation = ROLE_DEMO_STOP };
    xQueueSend(jobs, &job, portMAX_DELAY);
    xSemaphoreTake(stopped, portMAX_DELAY);
    vQueueDelete(jobs); vSemaphoreDelete(stopped);
    jobs = NULL; stopped = NULL; done = NULL; done_data = NULL;
}
