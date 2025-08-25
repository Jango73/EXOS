
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef BASE_H_INCLUDED
#define BASE_H_INCLUDED

/***************************************************************************/

#define __EXOS__

/***************************************************************************/

#pragma pack(1)

/***************************************************************************/
// Storage classes

#define CONST const
#define FAR far
#define NAKEDCALL __declspec(naked)
#define EXOSAPI
#define APIENTRY
#define REGISTER register

/***************************************************************************/
// Basic types

typedef unsigned char U8;
typedef signed char I8;
typedef unsigned short U16;
typedef signed short I16;
typedef unsigned long U32;
typedef signed long I32;
typedef unsigned int UINT;
typedef signed int INT;

typedef U32 LINEAR;
typedef U32 PHYSICAL;
typedef U8* LPPAGEBITMAP;

/***************************************************************************/

#define MAX_U8 ((U8)0xFF)
#define MAX_U16 ((U16)0xFFFF)
#define MAX_U32 ((U32)0xFFFFFFFF)
// #define MAX_U64 0xFFFFFFFFFFFFFFFF

/***************************************************************************/

typedef struct __attribute__((packed)) tag_U64 {
    U32 LO;
    U32 HI;
} U64;

/***************************************************************************/

typedef struct __attribute__((packed)) tag_U128 {
    U64 LO;
    U64 HI;
} U128;

/***************************************************************************/

typedef void* LPVOID;
typedef const void* LPCVOID;

/***************************************************************************/

typedef void (*VOIDFUNC)(void);
typedef U32 (*TASKFUNC)(LPVOID Param);

/***************************************************************************/
// Boolean type

typedef U32 BOOL;

#ifndef FALSE
#define FALSE ((BOOL)0)
#endif

#ifndef TRUE
#define TRUE ((BOOL)1)
#endif

/***************************************************************************/
// Utilities

#define UNUSED(x)               (void)(x)
#define SAFE_USE(a)             if ((a) != NULL)
#define SAFE_USE_2(a,b)         if ((a) != NULL && (b) != NULL)
#define SAFE_USE_VALID(a)       if ((a) != NULL && IsValidMemory(a))
#define SAFE_USE_VALID_2(a,b)   if ((a) != NULL && IsValidMemory(a) && (b) != NULL && IsValidMemory(b))
#define SAFE_USE_VALID_ID(a,i)  if ((a) != NULL && IsValidMemory(a) && (a->ID == i))

// Put CPU to sleep forever: disable IRQs, halt, and loop.
// Works with GCC/Clang (AT&T syntax). Uses a local numeric label and a memory
// clobber.
#define DO_THE_SLEEPING_BEAUTY       \
    do {                      \
        __asm__ __volatile__( \
            "1:\n\t"          \
            "cli\n\t"         \
            "hlt\n\t"         \
            "jmp 1b\n\t"      \
            :                 \
            :                 \
            : "memory");      \
    } while (0)

/***************************************************************************/
// NULL values

#ifndef NULL
#define NULL 0
#endif

#define NULL8 ((U8)0)
#define NULL16 ((U16)0)
#define NULL32 ((U32)0)
#define NULL64 ((U64)0)
#define NULL128 ((U128)0)

/***************************************************************************/
// Time values

#define INFINITY 0xFFFFFFFF

/***************************************************************************/
// Some machine constants

#define N_1B ((U32)0x00000001)
#define N_2B ((U32)0x00000002)
#define N_4B ((U32)0x00000004)
#define N_8B ((U32)0x00000008)
#define N_1KB ((U32)0x00000400)
#define N_2KB ((U32)0x00000800)
#define N_4KB ((U32)0x00001000)
#define N_8KB ((U32)0x00002000)
#define N_16KB ((U32)0x00004000)
#define N_32KB ((U32)0x00008000)
#define N_64KB ((U32)0x00010000)
#define N_128KB ((U32)0x00020000)
#define N_256KB ((U32)0x00040000)
#define N_512KB ((U32)0x00080000)
#define N_1MB ((U32)0x00100000)
#define N_2MB ((U32)0x00200000)
#define N_3MB ((U32)0x00300000)
#define N_4MB ((U32)0x00400000)
#define N_8MB ((U32)0x00800000)
#define N_16MB ((U32)0x01000000)
#define N_32MB ((U32)0x02000000)
#define N_64MB ((U32)0x04000000)
#define N_128MB ((U32)0x08000000)
#define N_1GB ((U32)0x40000000)
#define N_2GB ((U32)0x80000000)
#define N_4GB ((U32)0xFFFFFFFF)

#define N_1KB_M1 (N_1KB - 1)
#define N_4KB_M1 (N_4KB - 1)
#define N_1MB_M1 (N_1MB - 1)
#define N_4MB_M1 (N_4MB - 1)
#define N_1GB_M1 (N_1GB - 1)
#define N_2GB_M1 (N_2GB - 1)

/***************************************************************************/

#define MUL_2 1
#define MUL_4 2
#define MUL_8 3
#define MUL_16 4
#define MUL_32 5
#define MUL_64 6
#define MUL_128 7
#define MUL_256 8
#define MUL_512 9
#define MUL_1KB 10
#define MUL_2KB 11
#define MUL_4KB 12
#define MUL_8KB 13
#define MUL_16KB 14
#define MUL_32KB 15
#define MUL_64KB 16
#define MUL_128KB 17
#define MUL_256KB 18
#define MUL_512KB 19
#define MUL_1MB 20
#define MUL_2MB 21
#define MUL_4MB 22
#define MUL_8MB 23
#define MUL_16MB 24
#define MUL_32MB 25
#define MUL_64MB 26
#define MUL_128MB 27
#define MUL_256MB 28
#define MUL_512MB 29
#define MUL_1GB 30
#define MUL_2GB 31
#define MUL_4GB 32

/***************************************************************************/
// Bit values

#define BIT_0 0x0001
#define BIT_1 0x0002
#define BIT_2 0x0004
#define BIT_3 0x0008
#define BIT_4 0x0010
#define BIT_5 0x0020
#define BIT_6 0x0040
#define BIT_7 0x0080
#define BIT_8 0x0100
#define BIT_9 0x0200
#define BIT_10 0x0400
#define BIT_11 0x0800
#define BIT_12 0x1000
#define BIT_13 0x2000
#define BIT_14 0x4000
#define BIT_15 0x8000

/***************************************************************************/

#define BIT_0_VALUE(a) (((a) >> 0) & 1)
#define BIT_1_VALUE(a) (((a) >> 1) & 1)
#define BIT_2_VALUE(a) (((a) >> 2) & 1)
#define BIT_3_VALUE(a) (((a) >> 3) & 1)
#define BIT_4_VALUE(a) (((a) >> 4) & 1)
#define BIT_5_VALUE(a) (((a) >> 5) & 1)
#define BIT_6_VALUE(a) (((a) >> 6) & 1)
#define BIT_7_VALUE(a) (((a) >> 7) & 1)

#define BIT_8_VALUE(a) (((a) >> 8) & 1)
#define BIT_9_VALUE(a) (((a) >> 9) & 1)
#define BIT_10_VALUE(a) (((a) >> 10) & 1)
#define BIT_11_VALUE(a) (((a) >> 11) & 1)
#define BIT_12_VALUE(a) (((a) >> 12) & 1)
#define BIT_13_VALUE(a) (((a) >> 13) & 1)
#define BIT_14_VALUE(a) (((a) >> 14) & 1)
#define BIT_15_VALUE(a) (((a) >> 15) & 1)

/***************************************************************************/
// This macro gives the offset of a structure member

#define MEMBER_OFFSET(s, m) ((U32)(&(((s*)NULL)->m)))

/***************************************************************************/
// ASCII string types

typedef U8 STR;
typedef STR* LPSTR;
typedef CONST STR* LPCSTR;

#define TEXT(a) ((LPCSTR)a)

/***************************************************************************/
// Unicode string types

typedef U16 USTR;
typedef USTR* LPUSTR;
typedef CONST USTR* LPCUSTR;

/***************************************************************************/
// Common ASCII character values

#define STR_NULL ((STR)'\0')
#define STR_RETURN ((STR)'\r')
#define STR_NEWLINE ((STR)'\n')
#define STR_TAB ((STR)'\t')
#define STR_SPACE ((STR)' ')
#define STR_QUOTE ((STR)'"')
#define STR_SCORE ((STR)'_')
#define STR_DOT ((STR)'.')
#define STR_COLON ((STR)':')
#define STR_SLASH ((STR)'/')
#define STR_BACKSLASH ((STR)'\\')
#define STR_PLUS ((STR)'+')
#define STR_MINUS ((STR)'-')

/***************************************************************************/
// Common Unicode character values

#define USTR_NULL ((USTR)'\0')
#define USTR_RETURN ((USTR)'\r')
#define USTR_NEWLINE ((USTR)'\n')
#define USTR_TAB ((USTR)'\t')
#define USTR_SPACE ((USTR)' ')
#define USTR_QUOTE ((USTR)'"')
#define USTR_SCORE ((USTR)'_')
#define USTR_DOT ((USTR)'.')
#define USTR_COLON ((USTR)':')
#define USTR_SLASH ((USTR)'/')
#define USTR_BACKSLASH ((USTR)'\\')
#define USTR_PLUS ((USTR)'+')
#define USTR_MINUS ((USTR)'-')

/***************************************************************************/

typedef struct tag_OBJECT {
    U32 ID;
    U32 References;
} OBJECT, *LPOBJECT;

/***************************************************************************/

typedef struct tag_SYSTEMTIME {
    U32 Year : 22;
    U32 Month : 4;
    U32 Day : 6;
    U32 Hour : 6;
    U32 Minute : 6;
    U32 Second : 6;
    U32 Milli : 10;
    U32 Unused : 4;
} SYSTEMTIME, *LPSYSTEMTIME;

/***************************************************************************
 * Handles - They are a pointer in reality, but called handles so that they
 * are not used in userland, otherwise you get a nice page fault, at best.
 ***************************************************************************/

typedef U32 HANDLE;

/***************************************************************************/
// Maximum string lengths

#define MAX_FS_LOGICAL_NAME 64
#define MAX_COMMAND_LINE 1024
#define MAX_PATH_NAME 1024
#define MAX_FILE_NAME 256
#define MAX_USER_NAME 128
#define MAX_NAME 128
#define MAX_PASSWORD 64

/***************************************************************************/

#define MAKE_VERSION(maj, min) ((U32)(((((U32)maj) & 0xFFFF) << 16) | (((U32)min) & 0xFFFF)))

#define UNSIGNED(val) *((U32*)(&(val)))
#define SIGNED(val) *((I32*)(&(val)))

/***************************************************************************/
// Color manipulations

typedef U32 COLOR;

#define MAKERGB(r, g, b) ((((COLOR)r & 0xFF) << 0x00) | (((COLOR)g & 0xFF) << 0x08) | (((COLOR)b & 0xFF) << 0x10))

#define MAKERGBA(r, g, b, a)                                                                   \
    ((((COLOR)r & 0xFF) << 0x00) | (((COLOR)g & 0xFF) << 0x08) | (((COLOR)b & 0xFF) << 0x10) | \
     (((COLOR)a & 0xFF) << 0x18))

#define SETRED(c, r) (((COLOR)c & 0xFFFFFF00) | ((COLOR)r << 0x00))
#define SETGREEN(c, g) (((COLOR)c & 0xFFFF00FF) | ((COLOR)g << 0x08))
#define SETBLUE(c, b) (((COLOR)c & 0xFF00FFFF) | ((COLOR)b << 0x10))
#define SETALPHA(c, a) (((COLOR)c & 0x00FFFFFF) | ((COLOR)a << 0x18))

/***************************************************************************/
// Common color values

#define COLOR_BLACK ((COLOR)0x00000000)
#define COLOR_GRAY25 ((COLOR)0x00404040)
#define COLOR_GRAY50 ((COLOR)0x00808080)
#define COLOR_GRAY75 ((COLOR)0x00C0C0C0)
#define COLOR_WHITE ((COLOR)0x00FFFFFF)
#define COLOR_RED ((COLOR)0x000000FF)
#define COLOR_GREEN ((COLOR)0x0000FF00)
#define COLOR_BLUE ((COLOR)0x00FF0000)
#define COLOR_DARK_RED ((COLOR)0x00000080)
#define COLOR_DARK_GREEN ((COLOR)0x00008000)
#define COLOR_DARK_BLUE ((COLOR)0x00800000)
#define COLOR_LIGHT_RED ((COLOR)0x008080FF)
#define COLOR_LIGHT_GREEN ((COLOR)0x0080FF80)
#define COLOR_LIGHT_BLUE ((COLOR)0x00FF8080)
#define COLOR_YELLOW ((COLOR)0x0000FFFF)
#define COLOR_CYAN ((COLOR)0x00FFFF00)
#define COLOR_PURPLE ((COLOR)0x00FF00FF)
#define COLOR_BROWN ((COLOR)0x00008080)
#define COLOR_DARK_CYAN ((COLOR)0x00808000)

/***************************************************************************/

#endif
