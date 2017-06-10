#ifndef TERMKCD_UTIL_H
#define TERMKCD_UTIL_H

// Include limits header
#include <limits.h>

unsigned int str_to_uint(const char* str, int* error_flag) {
    size_t len = strlen(str);
    size_t res = 0;
    if(len == 0) {
        (*error_flag) = 1; // 0-length; throw error 1
        return(0);
    }
    for(size_t n = 0; n < len; ++n) {
        if(str[n] >= '0' && str[n] <= '9') {
            if((len - n - 1) == 0)
                res += (unsigned int)(str[n] - '0');
            else {
                unsigned int pow = 10;
                for(size_t i = 1; i < (len - n - 1); ++i)
                    pow *= 10;
                res += (unsigned int)(str[n] - '0') * pow;
            }
        }
        else {
            (*error_flag) = 2; // Invalid char; throw error 2
            return(0);
        }
    }
    if(res > UINT_MAX)
        (*error_flag) = 3; // Too big; throw error 3 (more of a warning than an error, can ignore). Overflowed value still returned.
    return (unsigned int)res;
}

#endif
