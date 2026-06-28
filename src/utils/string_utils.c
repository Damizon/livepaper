#include "string_utils.h"

#include <stdarg.h>
#include <stdio.h>

int safe_snprintf(char *dst, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(dst, size, fmt, args);
    va_end(args);

    if (ret < 0 || (size_t)ret >= size)
        return 0;

    return 1;
}
