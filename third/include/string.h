
#ifndef string_h
#define string_h

#include "sys/types.h"

#error "MERDE"

// Memory manipulation functions - implemented in runtime
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

#endif	// string_h
