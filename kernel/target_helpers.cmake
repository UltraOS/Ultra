macro(ultra_kernel_targets_apply FN)
    foreach(TARGET IN LISTS ULTRA_KERNEL_TARGETS)
        cmake_language(CALL ${FN} ${TARGET} ${ARGN})
    endforeach()
endmacro()

function(ultra_compile_options)
    ultra_kernel_targets_apply(
        target_compile_options
        PRIVATE
        ${ARGN}
    )
    target_compile_options(
        ${ULTRA_KERNEL_OBJECTS}
        PRIVATE
        ${ARGN}
    )
endfunction()

function(ultra_compile_definitions)
    ultra_kernel_targets_apply(
        target_compile_definitions
        PRIVATE
        ${ARGN}
    )
    target_compile_definitions(
        ${ULTRA_KERNEL_OBJECTS}
        PRIVATE
        ${ARGN}
    )
endfunction()

function(ultra_link_options)
    ultra_kernel_targets_apply(
        ultra_target_link_options
        PRIVATE
        ${ARGN}
    )
endfunction()

function(ultra_link_libraries)
    ultra_kernel_targets_apply(
        target_link_libraries
        PRIVATE
        ${ARGN}
    )
endfunction()

function(ultra_properties)
    ultra_kernel_targets_apply(
        set_target_properties
        PROPERTIES
        ${ARGN}
    )
endfunction()

function(ultra_sources)
    target_sources(
        ${ULTRA_KERNEL_OBJECTS}
        PRIVATE
        ${ARGN}
    )
endfunction()

function(ultra_include_directories)
    ultra_kernel_targets_apply(
        target_include_directories
        PRIVATE
        ${ARGN}
    )
    target_include_directories(
        ${ULTRA_KERNEL_OBJECTS}
        PRIVATE
        ${ARGN}
    )
endfunction()
