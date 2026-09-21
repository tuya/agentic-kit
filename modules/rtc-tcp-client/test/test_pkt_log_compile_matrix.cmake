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

foreach(level 0 2 3 4)
    foreach(sample_n 0 50)
        set(stem "tai_pkt_log_level_${level}_sample_${sample_n}")
        set(preprocessed "${OUTPUT_DIR}/${stem}.i")
        set(object_file "${OUTPUT_DIR}/${stem}.o")

        execute_process(
            COMMAND "${C_COMPILER}"
                    -E -P
                    "-DAGENTIC_KIT_LOG_LEVEL=${level}"
                    "-DAGENTIC_KIT_TAI_LOG_MEDIA_SAMPLE_N=${sample_n}"
                    ${include_args}
                    "${pkt_log_source}"
                    -o "${preprocessed}"
            RESULT_VARIABLE preprocess_rc
            OUTPUT_VARIABLE preprocess_stdout
            ERROR_VARIABLE preprocess_stderr
        )
        if(NOT preprocess_rc EQUAL 0)
            message(FATAL_ERROR
                "preprocess failed for level=${level}, sample=${sample_n}:\n"
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
                "compile failed for level=${level}, sample=${sample_n}:\n"
                "${compile_stdout}${compile_stderr}")
        endif()

        file(READ "${preprocessed}" source)
        string(FIND "${source}" "tai_log_packet(uint8_t" function_pos)
        string(FIND "${source}" "packet-type" formatter_pos)
        string(REGEX MATCH "log_emit\\([	\r\n ]*3[	\r\n ]*," info_sink "${source}")
        string(REGEX MATCH "log_emit\\([	\r\n ]*4[	\r\n ]*," debug_sink "${source}")

        if(level LESS 3)
            if(NOT function_pos EQUAL -1 OR NOT formatter_pos EQUAL -1)
                message(FATAL_ERROR
                    "level=${level}, sample=${sample_n}: formatter survived below INFO")
            endif()
            if(NOT "${info_sink}" STREQUAL "" OR NOT "${debug_sink}" STREQUAL "")
                message(FATAL_ERROR
                    "level=${level}, sample=${sample_n}: log sink survived below INFO")
            endif()
        else()
            if(function_pos EQUAL -1 OR formatter_pos EQUAL -1)
                message(FATAL_ERROR
                    "level=${level}, sample=${sample_n}: INFO formatter is missing")
            endif()
            if("${info_sink}" STREQUAL "")
                message(FATAL_ERROR
                    "level=${level}, sample=${sample_n}: INFO sink is missing")
            endif()

            if(level EQUAL 4 AND sample_n EQUAL 0)
                if("${debug_sink}" STREQUAL "")
                    message(FATAL_ERROR
                        "level=4, sample=0: DEBUG flood sink is missing")
                endif()
            elseif(NOT "${debug_sink}" STREQUAL "")
                message(FATAL_ERROR
                    "level=${level}, sample=${sample_n}: unexpected DEBUG sink")
            endif()
        endif()
    endforeach()
endforeach()

message(STATUS "tai_pkt_log compile-time matrix passed")
