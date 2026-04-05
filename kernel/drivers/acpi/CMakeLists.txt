include(uacpi/uacpi.cmake)

add_ultra_module(
    NAME acpi
    TYPE COMPILED_IN
    SOURCES
        acpi.c
        ${UACPI_SOURCES}
        uacpi_glue.c
    PUBLIC_DEFINITIONS
        UACPI_BAREBONES_MODE
        UACPI_OVERRIDE_LIBC
        UACPI_FORMATTED_LOGGING
    PUBLIC_INCLUDE_DIRS
        ${UACPI_INCLUDES}
    PRIVATE_INCLUDE_DIRS
        ${CMAKE_CURRENT_LIST_DIR}
)
