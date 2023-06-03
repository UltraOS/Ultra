function(add_ultra_ld_template)
    cmake_parse_arguments(
        SCRIPT
        ""
        "NAME;PATH;TEMPLATE_PATH;OUT_PATH"
        "DEPENDANT"
        ${ARGN}
    )

    if (NOT SCRIPT_NAME)
        message(FATAL_ERROR "NAME must be specified")
    endif ()

    if (SCRIPT_PATH)
        set(SCRIPT_TEMPLATE_PATH "${SCRIPT_PATH}.template")
        set(SCRIPT_OUT_PATH "${SCRIPT_PATH}")
    elseif (NOT SCRIPT_TEMPLATE_PATH OR NOT SCRIPT_OUT_PATH)
        message(FATAL_ERROR "TEMPLATE_PATH & OUT_PATH must be specified if PATH is omitted")
    endif ()

    get_target_property(DEPENDANT_INCLUDES ${SCRIPT_DEPENDANT} INCLUDE_DIRECTORIES)
    list(TRANSFORM DEPENDANT_INCLUDES PREPEND "-I")

    get_target_property(DEPENDANT_DEFINITIONS ${SCRIPT_DEPENDANT} COMPILE_DEFINITIONS)
    list(TRANSFORM DEPENDANT_DEFINITIONS PREPEND "-D")

    set(SCRIPT_DEP_FILE "${SCRIPT_NAME}.d")

    if (${CMAKE_VERSION} VERSION_GREATER "3.20.0")
        cmake_policy(PUSH)
        cmake_policy(SET CMP0116 NEW)
        set(DEPFILE_ARGS "DEPFILE;${SCRIPT_DEP_FILE}")
    else ()
        message(
            WARNING
            "CMake ${CMAKE_VERSION} doesn't support DEPFILE, "
            "linker script will not get re-preprocessed automatically "
            "when template changes! (this requires at least 3.20.0)"
        )
    endif ()

    add_custom_command(
        OUTPUT
        ${SCRIPT_OUT_PATH}
        COMMAND
        ${CMAKE_C_COMPILER} -E -xc -P ${SCRIPT_TEMPLATE_PATH}
        -Wp,-MD,${SCRIPT_DEP_FILE} -Wp,-MT,${SCRIPT_OUT_PATH}
        ${DEPENDANT_INCLUDES} ${DEPENDANT_DEFINITIONS} -o ${SCRIPT_OUT_PATH}
        DEPENDS
        ${SCRIPT_TEMPLATE_PATH}
        ${DEPFILE_ARGS}
        COMMAND_EXPAND_LISTS
    )

    if (${CMAKE_VERSION} VERSION_GREATER "3.20.0")
        CMAKE_POLICY(POP)
    endif ()


    add_custom_target(${SCRIPT_NAME} ALL DEPENDS ${SCRIPT_OUT_PATH})
    add_dependencies(${SCRIPT_DEPENDANT} ${SCRIPT_NAME})
    set_target_properties(${SCRIPT_DEPENDANT} PROPERTIES LINK_DEPENDS ${SCRIPT_OUT_PATH})
endfunction()
