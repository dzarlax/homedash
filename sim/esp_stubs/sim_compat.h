#pragma once

#include <time.h>

#if defined(_MSC_VER)
static inline struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return localtime_s(result, timep) == 0 ? result : 0;
}
#endif
