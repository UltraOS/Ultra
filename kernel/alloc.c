#include <alloc.h>

void *alloc(size_t size, enum alloc_behavior behavior)
{
    UNREFERENCED_PARAMETER(size);
    UNREFERENCED_PARAMETER(behavior);

    return NULL;
}

void free(void *ptr)
{
    UNREFERENCED_PARAMETER(ptr);
}
