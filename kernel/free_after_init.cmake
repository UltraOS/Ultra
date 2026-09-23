function(setup_free_after_init_check)
    set(
        COMBINED_OBJ
        "${CMAKE_CURRENT_BINARY_DIR}/combined_${ULTRA_KERNEL_OBJECTS}.o"
    )
    set(
        CHECK_SCRIPT
        "${ULTRA_SCRIPTS_DIR}/check_free_after_init_references.py"
    )

    add_custom_command(
        TARGET ${ULTRA_KERNEL_BASE} PRE_LINK
        COMMAND ${CMAKE_LINKER} -r ${ULTRA_ALL_OBJECTS} -o ${COMBINED_OBJ}
        COMMAND Python3::Interpreter ${CHECK_SCRIPT} ${COMBINED_OBJ}
        COMMENT
        "Verifying there are no illegal references to free-after-init data..."
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
endfunction()
