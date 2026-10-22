#ifndef BIZ_CLIENT_CONFIG_DEFAULTS_H
#define BIZ_CLIENT_CONFIG_DEFAULTS_H

/* Load integrator overrides before applying module defaults. */
#include "log.h"

#ifndef AGENTIC_KIT_BIZ_LOG_LEVEL
#define AGENTIC_KIT_BIZ_LOG_LEVEL AGENTIC_KIT_LOG_LEVEL
#endif

/* A module can lower the SDK-wide ceiling, never raise it. */
#if AGENTIC_KIT_BIZ_LOG_LEVEL > AGENTIC_KIT_LOG_LEVEL
#undef AGENTIC_KIT_BIZ_LOG_LEVEL
#define AGENTIC_KIT_BIZ_LOG_LEVEL AGENTIC_KIT_LOG_LEVEL
#endif

#endif
