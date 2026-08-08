function(add_ultra_module)
    cmake_parse_arguments(
        MODULE
        ""
        "NAME;CONFIG"
        "SOURCES;PUBLIC_CFLAGS;PRIVATE_CFLAGS;PUBLIC_DEFINITIONS;PRIVATE_DEFINITIONS;PUBLIC_INCLUDE_DIRS;PRIVATE_INCLUDE_DIRS"
        ${ARGN}
    )

    if (NOT MODULE_NAME OR NOT MODULE_SOURCES OR NOT MODULE_CONFIG)
        message(FATAL_ERROR "NAME, SOURCES, and CONFIG must be specified")
    endif ()

    string(PREPEND MODULE_CONFIG "CONFIG_")
    if (NOT ${MODULE_CONFIG})
        return()
    endif ()

    set(MODULE_OBJECT_TARGET "${MODULE_NAME}-objects")
    add_library(
        ${MODULE_OBJECT_TARGET}
        OBJECT
        ${MODULE_SOURCES}
    )

    ultra_compile_options(${MODULE_PUBLIC_CFLAGS})
    ultra_compile_definitions(${MODULE_PUBLIC_DEFINITIONS})
    ultra_include_directories(${MODULE_PUBLIC_INCLUDE_DIRS})

    # Consume kernel-wide flags via generator expressions instead of
    # linking ${ULTRA_KERNEL_IFACE}: this picks up the final values no
    # matter when this module was added, and keeps the kernel-wide
    # flags in front so that private flags may override them (linking
    # would order the target's own flags first)
    ultra_target_compile_options(
        ${MODULE_OBJECT_TARGET}
        PRIVATE
        $<TARGET_PROPERTY:${ULTRA_KERNEL_IFACE},INTERFACE_COMPILE_OPTIONS>
        ${MODULE_PRIVATE_CFLAGS}
    )
    target_compile_definitions(
        ${MODULE_OBJECT_TARGET}
        PRIVATE
        $<TARGET_PROPERTY:${ULTRA_KERNEL_IFACE},INTERFACE_COMPILE_DEFINITIONS>
        ${MODULE_PRIVATE_DEFINITIONS}
    )
    target_include_directories(
        ${MODULE_OBJECT_TARGET}
        PRIVATE
        $<TARGET_PROPERTY:${ULTRA_KERNEL_IFACE},INTERFACE_INCLUDE_DIRECTORIES>
        ${MODULE_PRIVATE_INCLUDE_DIRS}
    )

    if (${MODULE_CONFIG} STREQUAL "m")
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

        # Runtime modules are dynamically linked against the kernel
        # once loaded, so they must contain final machine code: there
        # is nothing for LTO to optimize against, and the relocatable
        # link would embed compiler IR instead of machine code (GNU
        # ld), or codegen behind our back with default flags (lld).
        # This comes after the kernel-wide flags, so it overrides
        # -flto if it's enabled.
        target_compile_options(
            ${MODULE_OBJECT_TARGET}
            PRIVATE
            -fno-lto
        )
    else ()
        set_property(
            GLOBAL APPEND PROPERTY
            ULTRA_OBJECT_TARGETS
            ${MODULE_OBJECT_TARGET}
        )
        ultra_link_libraries(${MODULE_OBJECT_TARGET})
    endif ()
endfunction()
