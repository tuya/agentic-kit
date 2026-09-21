/*
 * agentic_kit_config.h -- test-build override for the iot-client test
 * executables (and the tuya-ble test executable, which compiles the ble
 * sources into itself and links the iot test library), picked up by
 * common/log.h's override search.
 *
 * The SDK's log dispatch is decided HERE, at compile time: every TU of
 * the build -- SDK sources included -- routes through test_log_sink.
 * Capture/quiet/count are modes inside that one function (test_log.h),
 * not a runtime handler swap. Integrator-facing docs:
 * docs-site/docs/guides/compile-time-knobs.md.
 *
 * The prototype is spelled with plain int (log_level_t's underlying
 * type) and guarded extern "C" so this works from C++ TUs without
 * pulling log.h back in recursively.
 */
#ifdef __cplusplus
extern "C" {
#endif
void test_log_sink(int level, const char *tag, const char *fmt, ...);
#ifdef __cplusplus
}
#endif

#define AGENTIC_KIT_LOG(level, tag, fmt, ...) \
    test_log_sink(level, tag, fmt, ##__VA_ARGS__)
