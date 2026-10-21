#pragma once
#include "tuya_ai_role.h"

typedef enum {
    ROLE_DEMO_LOAD, ROLE_DEMO_BIND, ROLE_DEMO_EDIT, ROLE_DEMO_DELETE,
    ROLE_DEMO_LANGUAGES, ROLE_DEMO_TIMBRES
} role_demo_operation_t;

/* Value-only message: safe to copy from a UI/keypad callback into a queue. */
typedef struct {
    role_demo_operation_t operation;
    tuya_ai_role_type_t type;
    char role_id[TUYA_AI_ROLE_ID_LEN];
    bool set_name, set_lang, set_timbre, set_speed;
    char name[TUYA_AI_ROLE_NAME_LEN];
    char lang[TUYA_AI_ROLE_LANG_LEN];
    char timbre[TUYA_AI_ROLE_ID_LEN];
    double speed;
    int page_no, page_size; /* LOAD custom list and TIMBRES; >0 */
} role_demo_job_t;

/* Runs on worker. Borrowed arguments expire on return; marshal UI updates. */
/* change_accepted distinguishes a successful write followed by failed refresh:
 * do not retry creation just because rc (refresh) is nonzero in that case. */
typedef void (*role_demo_done_fn)(role_demo_operation_t, int rc, bool change_accepted,
                                  const iot_atop_rejection_t *, void *user_data);

/* Call once from app owner after activation/time sync, not a receive callback.
 * One instance in this example; the SDK itself permits independent contexts. */
bool role_management_start(iot_client_t *, role_demo_done_fn, void *user_data);
/* Nonblocking. false means not started/full/invalid: show busy, do not claim success.
 * Do not race start/stop with producers. No network work happens here. */
bool role_management_submit(const role_demo_job_t *);
/* Blocks until in-flight HTTPS finishes. First quiesce UI producers; call from
 * owner (never worker/callback), and only then destroy the borrowed IoT client. */
void role_management_stop(void);
