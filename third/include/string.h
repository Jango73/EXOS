
#ifndef string_h
#define string_h

#include "sys/types.h"

extern unsigned strcmp(const char*, const char*);
extern int strncmp(const char*, const char*, unsigned);
extern char* strstr(const char* haystack, const char* needle);
extern char* strchr(const char* string, int character);
extern void memset(void* dest, int c, size_t n);
extern void memcpy(void* dest, const void* src, size_t n);
extern void* memmove(void* dest, const void* src, size_t n);
extern int memcmp(const void* s1, const void* s2, size_t n);
extern unsigned strlen(const char* str);
extern char* strcpy(char* dest, const char* src);

#endif	// string_h
