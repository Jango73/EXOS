#ifndef THIRD_INCLUDE_STRING_H
#define THIRD_INCLUDE_STRING_H

#include "sys/types.h"

// Memory manipulation helpers provided by the runtime
void memcpy(void* destination, const void* source, size_t length);
void* memchr(const void* buffer, int value, size_t length);
int memcmp(const void* left, const void* right, size_t length);
void* memmove(void* destination, const void* source, size_t length);
void memset(void* buffer, int value, size_t length);

// Basic string helpers provided by the runtime
size_t strlen(const char* string);
char* strcpy(char* destination, const char* source);
char* strncpy(char* destination, const char* source, size_t length);
char* strcat(char* destination, const char* source);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, size_t length);
char* strchr(const char* string, int character);
char* strstr(const char* haystack, const char* needle);

#endif  // THIRD_INCLUDE_STRING_H
