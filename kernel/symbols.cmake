function(ultra_symbol_file)
    cmake_parse_arguments(
        ARG
        ""
        "OUTPUT_PATH;BINARY;DEPENDENCY"
        ""
        ${ARGN}
    )
    if (ARG_BINARY)
        set(BINARY_FLAGS "--binary;${ARG_BINARY}")
    endif ()

    if (ARG_DEPENDENCY)
        set(DEPENDENCY_FLAGS "DEPENDS;${ARG_BINARY}")
    endif ()

    add_custom_command(
        OUTPUT ${ARG_OUTPUT_PATH}
        COMMAND
        python3 ${ULTRA_SCRIPTS_DIR}/generate_symbol_tables.py
        ${ARG_OUTPUT_PATH} ${BINARY_FLAGS}
        ${DEPENDENCY_FLAGS}
        COMMAND_EXPAND_LISTS
    )
endfunction()

function(setup_symbol_table_link_steps NUM_RELINKS)
    set(SYMBOLS_STUB "kernel_symbols_stub.c")

    ultra_symbol_file(OUTPUT_PATH ${SYMBOLS_STUB})
    target_sources(
        ${ULTRA_KERNEL_BASE}
        PRIVATE
        ${SYMBOLS_STUB}
    )

    math(EXPR NUM_STEPS "${NUM_RELINKS} - 1")
    set (SYMBOL_FILES "")

    foreach (I RANGE ${NUM_STEPS})
        if (I EQUAL ${NUM_STEPS})
            set(THIS_TARGET "kernel-${ULTRA_ARCH_EXECUTION_MODE}")
            set(THIS_SYMBOL_FILE "kernel_symbols_final.c")
        else ()
            set(THIS_TARGET "kernel-${ULTRA_ARCH_EXECUTION_MODE}-prelim${I}")
            set(THIS_SYMBOL_FILE "kernel_symbols_prelim${I}.c")
        endif ()

        list(GET ULTRA_KERNEL_TARGETS -1 PREV_TARGET)

        ultra_symbol_file(
            OUTPUT_PATH
            ${THIS_SYMBOL_FILE}
            BINARY
            "$<TARGET_FILE:${PREV_TARGET}>"
            DEPENDENCY
            ${PREV_TARGET}
        )

        list(APPEND ULTRA_KERNEL_TARGETS ${THIS_TARGET})
        add_executable(${THIS_TARGET} ${THIS_SYMBOL_FILE})
    endforeach ()

    set(FINAL_SYMBOL_FILE "kernel_symbols_stability_check.c")
    ultra_symbol_file(
        OUTPUT_PATH
        ${FINAL_SYMBOL_FILE}
        BINARY
        "$<TARGET_FILE:${THIS_TARGET}>"
        DEPENDENCY
        ${THIS_TARGET}
    )

    add_custom_target(
        ensure-symbol-table-stabilized
        ALL
        COMMAND
        ${CMAKE_COMMAND} -E compare_files
        ${THIS_SYMBOL_FILE} ${FINAL_SYMBOL_FILE}
        DEPENDS
        ${THIS_SYMBOL_FILE} ${FINAL_SYMBOL_FILE}
        COMMENT
        "Ensuring the symbol table has stabilized"
    )

    set(ULTRA_KERNEL_TARGETS "${ULTRA_KERNEL_TARGETS}" PARENT_SCOPE)
endfunction()
