function(add_ultra_module)
    cmake_parse_arguments(
        MODULE
        ""
        "NAME;TYPE"
        "SOURCES;PUBLIC_CFLAGS;PRIVATE_CFLAGS;PUBLIC_DEFINITIONS;PRIVATE_DEFINITIONS;PUBLIC_INCLUDE_DIRS;PRIVATE_INCLUDE_DIRS"
        ${ARGN}
    )

    if (NOT MODULE_NAME OR NOT MODULE_SOURCES)
        message(FATAL_ERROR "NAME AND SOURCES must be specified")
    endif ()

    if (MODULE_TYPE STREQUAL "DISABLED")
        return()
    endif ()

    set(MODULE_OBJECT_TARGET "${MODULE_NAME}-objects")
    add_library(
        ${MODULE_OBJECT_TARGET}
        OBJECT
        ${MODULE_SOURCES}
    )

    ultra_compile_options(${MODULE_PUBLIC_CFLAGS})
    get_target_property(ULTRA_CFLAGS ${ULTRA_KERNEL_OBJECTS} COMPILE_OPTIONS)
    ultra_target_compile_options(
        ${MODULE_OBJECT_TARGET}
        PRIVATE
        ${ULTRA_CFLAGS}
        ${MODULE_PRIVATE_CFLAGS}
    )

    ultra_include_directories(${MODULE_PUBLIC_INCLUDE_DIRS})
    get_target_property(ULTRA_INCLUDES ${ULTRA_KERNEL_OBJECTS} INCLUDE_DIRECTORIES)
    target_include_directories(
        ${MODULE_OBJECT_TARGET}
        PRIVATE
        ${ULTRA_INCLUDES}
        ${MODULE_PRIVATE_INCLUDE_DIRS}
    )

    if (MODULE_TYPE STREQUAL "COMPILED_IN")
        set_property(
            GLOBAL APPEND PROPERTY
            ULTRA_OBJECT_TARGETS
            ${MODULE_OBJECT_TARGET}
        )
        ultra_link_libraries(${MODULE_OBJECT_TARGET})
    elseif (MODULE_TYPE STREQUAL "RUNTIME")
        set(MODULE_OUTPUT "${CMAKE_BINARY_DIR}/${MODULE_NAME}.ko")

        # We use this "hack" because cmake doesn't offer an easy way to override
        # link flags for a specific target other than CMAKE_C_LINK_EXECUTABLE,
        # which has a global scope.
        add_custom_command(
            OUTPUT
            ${MODULE_OUTPUT}
            DEPENDS
            ${MODULE_OBJECT_TARGET} ${MODULE_SOURCES}
            COMMAND
            ${CMAKE_LINKER}
            -r
            $<TARGET_OBJECTS:${MODULE_OBJECT_TARGET}> -o ${MODULE_OUTPUT}
            COMMAND_EXPAND_LISTS
        )
        add_custom_target(
            ${MODULE_NAME}-module
            ALL DEPENDS
            ${MODULE_OUTPUT}
        )

        target_compile_definitions(
            ${MODULE_OBJECT_TARGET}
            PRIVATE
            ULTRA_RUNTIME_MODULE
        )
    else ()
        message(FATAL_ERROR "Invalid module type '${MODULE_TYPE}'")
    endif ()

    ultra_compile_definitions(${MODULE_PUBLIC_DEFINITIONS})
    get_target_property(ULTRA_DEFINITIONS ${ULTRA_KERNEL_OBJECTS} COMPILE_DEFINITIONS)
    target_compile_definitions(
        ${MODULE_OBJECT_TARGET}
        PRIVATE
        ${ULTRA_DEFINITIONS}
        ${MODULE_PRIVATE_DEFINITIONS}
    )
endfunction()
