
#ifndef sys_types_h
#define sys_types_h

// Basic types

#if defined(_MSC_VER)

typedef unsigned long uLong;
typedef int off_t;
typedef int time_t;
typedef unsigned int mode_t;
typedef unsigned int size_t;

#elif defined(__GNUC__) || defined(__clang__)

typedef unsigned int uLong;
typedef long off_t;
typedef long time_t;
typedef unsigned long mode_t;
typedef unsigned long size_t;

#else
    #error "Unsupported compiler for string.h"
#endif

#define NULL ((void*)0)

#endif	// sys_types_h
