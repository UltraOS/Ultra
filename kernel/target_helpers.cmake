macro(ultra_kernel_targets_apply FN)
    foreach(TARGET IN LISTS ULTRA_KERNEL_TARGETS)
        cmake_language(CALL ${FN} ${TARGET} ${ARGN})
    endforeach()
endmacro()

# All kernel-wide flags accumulate on ${ULTRA_KERNEL_IFACE} as usage
# requirements. Consumers either link against it or extract the final
# values with $<TARGET_PROPERTY:...>, which is evaluated at generate
# time, so the results don't depend on configure order.
function(ultra_compile_options)
    ultra_target_compile_options(
        ${ULTRA_KERNEL_IFACE}
        INTERFACE
        ${ARGN}
    )
endfunction()

function(ultra_compile_definitions)
    target_compile_definitions(
        ${ULTRA_KERNEL_IFACE}
        INTERFACE
        ${ARGN}
    )
endfunction()

function(ultra_link_options)
    ultra_target_link_options(
        ${ULTRA_KERNEL_IFACE}
        INTERFACE
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

function(ultra_sources_if CONFIG)
    if (NOT ${CONFIG})
        return()
    endif ()

    ultra_sources(${ARGN})
endfunction()

function(ultra_include_directories)
    target_include_directories(
        ${ULTRA_KERNEL_IFACE}
        INTERFACE
        ${ARGN}
    )
endfunction()
