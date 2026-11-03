#!/usr/bin/env python3
"""Compile/run the Business Client's log ceilings and override pickup."""

import argparse
from pathlib import Path
import subprocess
import tempfile


FIXTURE = r"""
#include <stdarg.h>
#include <string.h>

void test_log_sink(int level, const char *tag, const char *fmt, ...);
#include "service_internal.h"

#if AGENTIC_KIT_BIZ_LOG_LEVEL != TEST_EXPECT_LEVEL
#error "Business Client log ceiling was not clamped or picked up correctly"
#endif

static unsigned seen[5];
static unsigned evaluated;
static int failed;

static void record(int level, const char *fmt, const char *expected, va_list args)
{
    if (level < LOG_ERROR || level > LOG_DEBUG || strcmp(fmt, expected) ||
        va_arg(args, unsigned) != (unsigned)level) {
        failed = 1;
        return;
    }
    ++seen[level];
}

/* Defining both sinks keeps every matrix build independent of common/log.c. */
void log_emit(int level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    record(level, fmt, "[biz] value %u", args);
    va_end(args);
}

void test_log_sink(int level, const char *tag, const char *fmt, ...)
{
    va_list args;
    if (strcmp(tag, "biz")) failed = 1;
    va_start(args, fmt);
    record(level, fmt, "value %u", args);
    va_end(args);
}

int main(void)
{
    BIZ_LOGE("value %u", ++evaluated);
    BIZ_LOGW("value %u", ++evaluated);
    BIZ_LOGI("value %u", ++evaluated);
    BIZ_LOGD("value %u", ++evaluated);
    if (evaluated != TEST_EXPECT_LEVEL) return 1;
    for (unsigned level = LOG_ERROR; level <= LOG_DEBUG; ++level)
        if (seen[level] != (unsigned)(level <= TEST_EXPECT_LEVEL)) return 2;
    return failed ? 3 : 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True, help="C compiler executable")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    includes = [
        root / "common",
        root / "pal",
        root / "modules/biz-client/include",
        root / "modules/biz-client/src",
        root / "modules/iot-client/include",
        root / "third_party/cJSON",
    ]
    cases = []
    for global_level in range(5):
        for module_level in [None, *range(5), 9]:
            defines = {"AGENTIC_KIT_LOG_LEVEL": global_level}
            if module_level is not None:
                defines["AGENTIC_KIT_BIZ_LOG_LEVEL"] = module_level
            effective = min(global_level, module_level if module_level is not None else global_level)
            cases.append((f"global-{global_level}-biz-{module_level}", defines, effective, None))
    cases.extend([
        ("default", {}, 4, None),
        ("custom-header-and-sink", {}, 1, "explicit"),
        ("automatic-header-and-sink", {}, 2, "automatic"),
    ])

    with tempfile.TemporaryDirectory(prefix="agentic-biz-log-") as directory:
        work = Path(directory)
        source = work / "test.c"
        source.write_text(FIXTURE, encoding="utf-8")
        for name, defines, expected, config in cases:
            case_dir = work / name
            case_dir.mkdir()
            command = [args.cc, "-std=c99", "-Wall", "-Wextra", "-Werror"]
            command += [f"-I{path}" for path in [case_dir, *includes]]
            command += [f"-D{key}={value}" for key, value in defines.items()]
            command += [f"-DTEST_EXPECT_LEVEL={expected}"]
            if config:
                header = "business_options.h" if config == "explicit" else "agentic_kit_config.h"
                (case_dir / header).write_text(
                    "#define AGENTIC_KIT_LOG_LEVEL 3\n"
                    f"#define AGENTIC_KIT_BIZ_LOG_LEVEL {expected}\n"
                    "#define AGENTIC_KIT_LOG(level, tag, fmt, ...) "
                    "test_log_sink(level, tag, fmt, ##__VA_ARGS__)\n",
                    encoding="utf-8",
                )
                if config == "explicit":
                    command += [f'-DAGENTIC_KIT_USER_CONFIG="{header}"']
            executable = case_dir / "test"
            subprocess.run([*command, str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
    print(f"Business Client log compile matrix: {len(cases)}/{len(cases)} passed")


if __name__ == "__main__":
    main()
