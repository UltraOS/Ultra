function(setup_asm_offsets SOURCE)
    set(OFFSETS_FILE "${ULTRA_GENERATED_DIR}/asm_offsets.h")
    set(OFFSETS_SCRIPT "${ULTRA_SCRIPTS_DIR}/generate_asm_offsets.py")
    set(OFFSETS_OBJECTS "kernel-asm-offsets")

    add_library(${OFFSETS_OBJECTS} OBJECT ${SOURCE})
    target_link_libraries(${OFFSETS_OBJECTS} PRIVATE ${ULTRA_KERNEL_IFACE})
    set_property(
        SOURCE ${SOURCE}
        TARGET_DIRECTORY ${OFFSETS_OBJECTS}
        APPEND
        PROPERTY COMPILE_OPTIONS
        -S -fno-lto -g0
    )

    # Clang warns about -c being unused since we override it with -S here
    if (CONFIG_TOOLCHAIN_CLANG)
        set_property(
            SOURCE ${SOURCE}
            TARGET_DIRECTORY ${OFFSETS_OBJECTS}
            APPEND
            PROPERTY COMPILE_OPTIONS
            -Wno-unused-command-line-argument
        )
    endif ()

    add_custom_command(
        OUTPUT ${OFFSETS_FILE}
        COMMAND
        Python3::Interpreter ${OFFSETS_SCRIPT}
        $<TARGET_OBJECTS:${OFFSETS_OBJECTS}> ${OFFSETS_FILE}
        DEPENDS
        ${OFFSETS_OBJECTS} $<TARGET_OBJECTS:${OFFSETS_OBJECTS}>
        ${OFFSETS_SCRIPT}
        COMMENT "Generating asm offsets..."
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    add_custom_target(asm-offsets DEPENDS ${OFFSETS_FILE})
    add_dependencies(${ULTRA_KERNEL_OBJECTS} asm-offsets)
endfunction()
