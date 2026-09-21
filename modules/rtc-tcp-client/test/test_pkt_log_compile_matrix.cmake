cmake_minimum_required(VERSION 3.20)

foreach(required_var SOURCE_DIR OUTPUT_DIR C_COMPILER)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

set(pkt_log_source
    "${SOURCE_DIR}/modules/rtc-tcp-client/src/tai_pkt_log.c")
set(include_args
    "-I${SOURCE_DIR}/modules/rtc-tcp-client/include"
    "-I${SOURCE_DIR}/modules/rtc-tcp-client/src"
    "-I${SOURCE_DIR}/common"
    "-I${SOURCE_DIR}/pal"
    "-I${SOURCE_DIR}/third_party/mbedtls/include"
)

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

# Preprocess AND compile one configuration, then check the sinks that
# survived. `assert_level` is the ceiling tai_pkt_log.c should have acted
# under, whichever knob (global or per-module) supplied it.
function(run_pkt_log_combo stem)
    set(preprocessed "${OUTPUT_DIR}/${stem}.i")
    set(object_file "${OUTPUT_DIR}/${stem}.o")

    execute_process(
        COMMAND "${C_COMPILER}"
                -E -P
                ${extra_defines}
                ${include_args}
                "${pkt_log_source}"
                -o "${preprocessed}"
        RESULT_VARIABLE preprocess_rc
        OUTPUT_VARIABLE preprocess_stdout
        ERROR_VARIABLE preprocess_stderr
    )
    if(NOT preprocess_rc EQUAL 0)
        message(FATAL_ERROR
            "preprocess failed for ${stem}:\n"
            "${preprocess_stdout}${preprocess_stderr}")
    endif()

    # Compile the exact preprocessed translation unit too: the string/sink
    # assertions below must not pass merely because an invalid branch was
    # removed textually.
    execute_process(
        COMMAND "${C_COMPILER}" -c "${preprocessed}" -o "${object_file}"
        RESULT_VARIABLE compile_rc
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT compile_rc EQUAL 0)
        message(FATAL_ERROR
            "compile failed for ${stem}:\n"
            "${compile_stdout}${compile_stderr}")
    endif()

    file(READ "${preprocessed}" source)
    string(FIND "${source}" "tai_log_packet(uint8_t" function_pos)
    string(FIND "${source}" "packet-type" formatter_pos)
    string(REGEX MATCH "log_emit\\([\t\r\n ]*3[\t\r\n ]*," info_sink "${source}")
    string(REGEX MATCH "log_emit\\([\t\r\n ]*4[\t\r\n ]*," debug_sink "${source}")

    if(assert_level LESS 3)
        if(NOT function_pos EQUAL -1 OR NOT formatter_pos EQUAL -1)
            message(FATAL_ERROR
                "${stem}: formatter survived below INFO")
        endif()
        if(NOT "${info_sink}" STREQUAL "" OR NOT "${debug_sink}" STREQUAL "")
            message(FATAL_ERROR
                "${stem}: log sink survived below INFO")
        endif()
    else()
        if(function_pos EQUAL -1 OR formatter_pos EQUAL -1)
            message(FATAL_ERROR
                "${stem}: INFO formatter is missing")
        endif()
        if("${info_sink}" STREQUAL "")
            message(FATAL_ERROR
                "${stem}: INFO sink is missing")
        endif()

        if(assert_level EQUAL 4 AND sample_n EQUAL 0)
            if("${debug_sink}" STREQUAL "")
                message(FATAL_ERROR
                    "${stem}: DEBUG flood sink is missing")
            endif()
        elseif(NOT "${debug_sink}" STREQUAL "")
            message(FATAL_ERROR
                "${stem}: unexpected DEBUG sink")
        endif()
    endif()
endfunction()

# Dimension 1: the SDK-wide ceiling drives the compile-out (the per-module
# knob defaults to it when not defined).
foreach(level 0 2 3 4)
    foreach(sample_n 0 50)
        set(stem "tai_pkt_log_level_${level}_sample_${sample_n}")
        set(assert_level "${level}")
        set(extra_defines
            "-DAGENTIC_KIT_LOG_LEVEL=${level}"
            "-DAGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N=${sample_n}")
        run_pkt_log_combo("${stem}")
    endforeach()
endforeach()

# Dimension 2: the per-module ceiling drives the SAME compile-out with the
# SDK-wide ceiling pinned open at 4 -- AGENTIC_KIT_TAI_LOG_LEVEL must
# behave exactly as the global level did, without touching anything else.
foreach(tai_level 0 2 3 4)
    foreach(sample_n 0 50)
        set(stem "tai_pkt_log_global4_tai_${tai_level}_sample_${sample_n}")
        set(assert_level "${tai_level}")
        set(extra_defines
            "-DAGENTIC_KIT_LOG_LEVEL=4"
            "-DAGENTIC_KIT_TAI_LOG_LEVEL=${tai_level}"
            "-DAGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N=${sample_n}")
        run_pkt_log_combo("${stem}")
    endforeach()
endforeach()

# Dimension 3: the module ceiling is LOWER-ONLY -- a value above the
# SDK-wide ceiling is clamped to it in tai_config_defaults.h, so these
# builds must compile exactly as the lower global ceiling alone would:
# no formatter at 2, no flood branch at 3, nothing resurrected.
foreach(over "2;3;0" "2;4;50" "3;4;0")
    list(GET over 0 global_level)
    list(GET over 1 tai_level)
    list(GET over 2 sample_n)
    set(stem "tai_pkt_log_over_global${global_level}_tai${tai_level}_sample_${sample_n}")
    set(assert_level "${global_level}")
    set(extra_defines
        "-DAGENTIC_KIT_LOG_LEVEL=${global_level}"
        "-DAGENTIC_KIT_TAI_LOG_LEVEL=${tai_level}"
        "-DAGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N=${sample_n}")
    run_pkt_log_combo("${stem}")
endforeach()

message(STATUS "tai_pkt_log compile-time matrix passed")
