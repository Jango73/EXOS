
#ifndef stdlib_h
#define stdlib_h

#include "sys/types.h"

// Kernel stub for stdlib.h

void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);

#endif	// stdlib_h
