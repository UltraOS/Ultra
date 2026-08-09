set(ULTRA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(ULTRA_CONFIG_STAMPS_DIR "${ULTRA_GENERATED_DIR}/config-stamps")
set(ULTRA_OLDDEFCONFIG "${ULTRA_SCRIPTS_DIR}/kconfiglib/olddefconfig.py")

if (NOT EXISTS ${ULTRA_OLDDEFCONFIG})
    message (
        FATAL_ERROR
        "${ULTRA_OLDDEFCONFIG} doesn't exist!"
        "Did you forget to 'git submodule update --init'?"
    )
endif ()

# Make sure the generated directory always exists
execute_process(
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "${ULTRA_GENERATED_DIR}"
)

function(kconfig_sanitize CONFIG_FILE)
    file(GLOB_RECURSE KCONFIG_FILES "${CMAKE_SOURCE_DIR}/kernel/Kconfig*")
    list(APPEND KCONFIG_FILES "${CMAKE_SOURCE_DIR}/Kconfig")

    set_property(
        DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CONFIG_FILE}"
        "${KCONFIG_FILES}"
    )

    set(TMP_CONFIG_FILE "${CMAKE_BINARY_DIR}/.config.tmp")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CONFIG_FILE}" "${TMP_CONFIG_FILE}"
    )

    set(ENV{KCONFIG_CONFIG} "${TMP_CONFIG_FILE}")
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${ULTRA_OLDDEFCONFIG}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE SANITIZE_STATUS
        OUTPUT_VARIABLE OLDDEF_OUTPUT
    )
    unset(ENV{KCONFIG_CONFIG})

    if (NOT SANITIZE_STATUS EQUAL 0)
        message(
            FATAL_ERROR
            "Unable to sanitize kernel configuration, "
            "please fix Kconfig rules or the config itself"
        )
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TMP_CONFIG_FILE}"
        "${CONFIG_FILE}"
    )

    if (NOT OLDDEF_OUTPUT MATCHES "No change")
        message(STATUS "Updated ${CONFIG_FILE} to match Kconfig rules")
    endif ()
endfunction ()

function(kconfig_load KCONFIG_FILE)
    if (NOT EXISTS "${KCONFIG_FILE}")
        message(FATAL_ERROR "Invalid config file path: ${KCONFIG_FILE}")
    endif ()

    file(STRINGS "${KCONFIG_FILE}" LINES)
    set(CONFIG_HDR "")

    string(APPEND CONFIG_HDR "/*\n")
    string(APPEND CONFIG_HDR " * Auto-generated kernel configuration header\n")
    string(APPEND CONFIG_HDR " * Do not edit manually!\n")
    string(APPEND CONFIG_HDR " */\n\n")

    foreach (LINE IN LISTS LINES)
        if (LINE MATCHES "^# (CONFIG_[A-Za-z0-9_]+) is not set")
            set(${CMAKE_MATCH_1} OFF PARENT_SCOPE)
            string(APPEND CONFIG_HDR "// ${CMAKE_MATCH_1} is not set\n")
        elseif (LINE MATCHES "^(CONFIG_[A-Za-z0-9_]+)=y$")
            set(${CMAKE_MATCH_1} ON PARENT_SCOPE)
            string(APPEND CONFIG_HDR "#define ${CMAKE_MATCH_1} 1\n")
        elseif (LINE MATCHES "^(CONFIG_[A-Za-z0-9_]+)=m$")
            set(${CMAKE_MATCH_1} "m" PARENT_SCOPE)
            string(APPEND CONFIG_HDR "#define ${CMAKE_MATCH_1}_MODULE 1\n")
        elseif (LINE MATCHES "^(CONFIG_[A-Za-z0-9_]+)=\"(.*)\"$")
            set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" PARENT_SCOPE)
            string(APPEND CONFIG_HDR "#define ${CMAKE_MATCH_1} \"${CMAKE_MATCH_2}\"\n")
        elseif (LINE MATCHES "^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
            set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" PARENT_SCOPE)
            string(APPEND CONFIG_HDR "#define ${CMAKE_MATCH_1} ${CMAKE_MATCH_2}\n")
        endif ()
    endforeach ()

    file(WRITE "${CMAKE_BINARY_DIR}/config.h.tmp" "${CONFIG_HDR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/config.h.tmp"
        "${ULTRA_GENERATED_DIR}/config.h"
    )
endfunction ()

function(kconfig_sync_dependencies CONFIG_FILE)
    set(ENV{KCONFIG_CONFIG} "${CONFIG_FILE}")
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
        "${ULTRA_SCRIPTS_DIR}/sync_config_dependencies.py"
        "${ULTRA_CONFIG_STAMPS_DIR}"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE SYNC_STATUS
    )
    unset(ENV{KCONFIG_CONFIG})

    if (NOT SYNC_STATUS EQUAL 0)
        message(
            FATAL_ERROR
            "Unable to synchronize per-symbol config dependencies"
        )
    endif ()
endfunction ()
