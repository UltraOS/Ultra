function(setup_build_stamp)
    set(REVISION_FILE "${ULTRA_GENERATED_DIR}/git_revision")
    set(STAMP_FILE "${ULTRA_GENERATED_DIR}/build_stamp.h")
    set(STAMP_SCRIPT "${ULTRA_SCRIPTS_DIR}/generate_build_stamp.py")
    set(BANNER_OBJECTS "kernel-build-banner")

    add_custom_target(
        git-revision
        COMMAND
        Python3::Interpreter ${STAMP_SCRIPT} revision ${REVISION_FILE}
        --source-dir ${CMAKE_SOURCE_DIR}
        BYPRODUCTS ${REVISION_FILE}
        COMMENT "Checking the git revision"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${STAMP_FILE}
        COMMAND
        Python3::Interpreter ${STAMP_SCRIPT} stamp ${STAMP_FILE}
        --revision-file ${REVISION_FILE}
        DEPENDS
        git-revision ${REVISION_FILE} ${STAMP_SCRIPT} ${ULTRA_ALL_OBJECTS}
        COMMENT "Generating the build stamp"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    add_library(
        ${BANNER_OBJECTS}
        OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/build_banner.c ${STAMP_FILE}
    )
    add_dependencies(${BANNER_OBJECTS} ${ULTRA_OBJECT_TARGETS_FINAL})
    target_link_libraries(${BANNER_OBJECTS} PRIVATE ${ULTRA_KERNEL_IFACE})
    ultra_link_libraries(${BANNER_OBJECTS})
endfunction()
