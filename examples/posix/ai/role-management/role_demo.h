#ifndef ROLE_DEMO_H
#define ROLE_DEMO_H
#include "tuya_ai_role.h"
/* Synchronous command executor; call only in an application worker. */
int role_demo_execute(tuya_biz_client_t *biz, int argc, const char *const argv[]);
void role_demo_usage(void);
#endif
