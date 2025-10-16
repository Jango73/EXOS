
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Base

\************************************************************************/

#ifndef BASE_H_INCLUDED
#define BASE_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/
// Define __EXOS__

#define __EXOS__

/***************************************************************************/
// Target architecture detection

#if defined(__i386__) || defined(_M_IX86)
    #define __EXOS_ARCH_I386__
#elif defined(__x86_64__) || defined(_M_X64)
    #define __EXOS_ARCH_X86_64__
#else
    #error "Unsupported target architecture for EXOS"
#endif

/***************************************************************************/
// Check __SIZEOF_POINTER__ definition

#ifndef __SIZEOF_POINTER__
    #if defined(_MSC_VER)
        #if defined(_WIN64)
            #define __SIZEOF_POINTER__ 8
        #else
            #define __SIZEOF_POINTER__ 4
        #endif
    #elif defined(__GNUC__) || defined(__clang__)
        // GCC and Clang already define this, but we keep a fallback
        #if defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__)
            #define __SIZEOF_POINTER__ 8
        #else
            #define __SIZEOF_POINTER__ 4
        #endif
    #else
        #error "Cannot determine pointer size for this compiler."
    #endif
#endif

/***************************************************************************/
// Define __EXOS__ bit size

#if __SIZEOF_POINTER__ == 8
    #define __EXOS_64__
#else
    #define __EXOS_32__
#endif

/***************************************************************************/
// Validate architecture and ABI combination

#if defined(__EXOS_ARCH_I386__)
    #if __SIZEOF_POINTER__ != 4
        #error "i386 build requires 32-bit pointer size"
    #endif
#elif defined(__EXOS_ARCH_X86_64__)
    #if __SIZEOF_POINTER__ != 8
        #error "x86-64 build requires 64-bit pointer size"
    #endif
#endif

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// Storage classes

#define CONST const
#define FAR far
#define PACKED __attribute__((packed))
#define NAKEDCALL __declspec(naked)
#define NORETURN __attribute__((noreturn))
#define EXOSAPI
#define APIENTRY
#define REGISTER register

#define SECTION(a) __attribute__((section(a)))

/***************************************************************************/
// Basic types

#if defined(_MSC_VER)
    typedef unsigned __int8 U8;     // Unsigned byte
    typedef signed __int8 I8;       // Signed byte
    typedef unsigned __int16 U16;   // Unsigned short
    typedef signed __int16 I16;     // Signed short
    typedef unsigned __int32 U32;   // Unsigned long
    typedef signed __int32 I32;     // Signed long
    typedef unsigned int UINT;      // Unsigned register-sized integer
    typedef signed int INT;         // Signed register-sized integer
#elif defined(__GNUC__) || defined(__clang__)
    typedef unsigned char U8;       // Unsigned byte
    typedef signed char I8;         // Signed byte
    typedef unsigned short U16;     // Unsigned short
    typedef signed short I16;       // Signed short
    // Unix-land decided to inverse int and long logic ! What a good idea.
    typedef unsigned int U32;       // Unsigned long
    typedef signed int I32;         // Signed long
    typedef unsigned long UINT;     // Unsigned register-sized integer
    typedef signed long INT;        // Signed register-sized integer
#else
    #error "Unsupported compiler for Base.h"
#endif

/***************************************************************************/

typedef struct tag_U48 {
    U16 LO;
    U32 HI;
} U48;

/***************************************************************************/

#ifdef __EXOS_32__
    typedef struct tag_U64 {
        U32 LO;
        U32 HI;
    } U64;

    typedef struct tag_I64 {
        U32 LO;
        I32 HI;
    } I64;

    #define U64_0 { .LO = 0, .HI = 0 }
    #define U64_EQUAL(a, b) (a.LO == b.LO && a.HI == b.HI)
#else
    typedef unsigned long long  U64;
    typedef signed long long    I64;

    #define U64_0 0
    #define U64_EQUAL(a, b) (a == b)
#endif

/***************************************************************************/

typedef struct tag_U80 {
    U16 LO;
    U64 HI;
} U80;

/***************************************************************************/

typedef struct tag_U128 {
    U64 LO;
    U64 HI;
} U128;

/***************************************************************************/

#define MAX_U8 ((U8)0xFF)
#define MAX_U16 ((U16)0xFFFF)
#define MAX_U32 ((U32)0xFFFFFFFF)

#ifdef __EXOS_32__
    #define MAX_UINT ((UINT)0xFFFFFFFF)
#else
    #define MAX_UINT ((UINT)0xFFFFFFFFFFFFFFFFULL)
#endif

#ifdef __EXOS_64__
    #define MAX_U64 0xFFFFFFFFFFFFFFFF
#endif

/***************************************************************************/

typedef float F32;              // 32 bit float
typedef double F64;             // 64 bit float

typedef U32 SIZE;

typedef UINT LINEAR;            // Linear virtual address, paged or not
typedef UINT PHYSICAL;          // Physical address
typedef U8* LPPAGEBITMAP;       // Pointer to a page allocation bitmap

/************************************************************************/

#ifdef __KERNEL__

#if DEBUG_OUTPUT == 1
    #define DEBUG(a, ...) KernelLogText(LOG_DEBUG, (a), ##__VA_ARGS__)
#else
    #define DEBUG(a, ...)
#endif

#define VERBOSE(a, ...) KernelLogText(LOG_VERBOSE, (a), ##__VA_ARGS__)
#define WARNING(a, ...) KernelLogText(LOG_WARNING, (a), ##__VA_ARGS__)
#define ERROR(a, ...) KernelLogText(LOG_ERROR, (a), ##__VA_ARGS__)

#else

#if DEBUG_OUTPUT == 1
    #define DEBUG(a, ...) debug((a), ##__VA_ARGS__)
#else
    #define DEBUG(a, ...)
#endif

#define VERBOSE(a, ...)
#define WARNING(a, ...)
#define ERROR(a, ...)

#endif

/************************************************************************/

typedef void* LPVOID;
typedef const void* LPCVOID;

/************************************************************************/

typedef void (*VOIDFUNC)(void);
typedef U32 (*TASKFUNC)(LPVOID Param);

/************************************************************************/
// Boolean type

typedef UINT BOOL;

#ifndef FALSE
#define FALSE ((BOOL)0)
#endif

#ifndef TRUE
#define TRUE ((BOOL)1)
#endif

/************************************************************************/
// Utilities

#define UNUSED(x) (void)(x)
#define SAFE_USE(a) if ((a) != NULL)
#define SAFE_USE_2(a, b) if ((a) != NULL && (b) != NULL)
#define SAFE_USE_3(a, b, c) if ((a) != NULL && (b) != NULL && (c) != NULL)
#define SAFE_USE_ID(a, i) if ((a) != NULL && (a->TypeID == i))
#define SAFE_USE_ID_2(a, b, i) if ((a) != NULL && (a->TypeID == i) && (b) != NULL && (b->TypeID == i))
#define SAFE_USE_VALID(a) if ((a) != NULL && IsValidMemory((LINEAR)a))
#define SAFE_USE_VALID_2(a, b) if ((a) != NULL && IsValidMemory((LINEAR)a) && (b) != NULL && IsValidMemory((LINEAR)b))
#define SAFE_USE_VALID_ID(a, i) if ((a) != NULL && IsValidMemory((LINEAR)a) && ((a)->TypeID == i))
#define SAFE_USE_VALID_ID_2(a, b, i) if ((a) != NULL && IsValidMemory((LINEAR)a) && ((a)->TypeID == i) \
        && ((b) != NULL && IsValidMemory((LINEAR)b) && ((b)->TypeID == i)))

// This is called before dereferencing a user-provided pointer to a parameter structure
#define SAFE_USE_INPUT_POINTER(p, s) if ((p) != NULL && IsValidMemory((LINEAR)p) && (p)->Header.Size >= sizeof(s))

// Do an infinite loop
#define FOREVER while(1)

// Put CPU to sleep forever: disable IRQs, halt, and loop.
#define DO_THE_SLEEPING_BEAUTY \
    do {                       \
        __asm__ __volatile__(  \
            "1:\n\t"           \
            "cli\n\t"          \
            "hlt\n\t"          \
            "jmp 1b\n\t"       \
            :                  \
            :                  \
            : "memory");       \
    } while (0)

#define STRINGS_EQUAL(a,b) (StringCompare(a,b)==0)
#define STRINGS_EQUAL_NO_CASE(a,b) (StringCompareNC(a,b)==0)

/************************************************************************/
// NULL values

#ifndef NULL
#define NULL 0
#endif

#define NULL8 ((U8)0)
#define NULL16 ((U16)0)
#define NULL32 ((U32)0)
#define NULL64 ((U64)0)
#define NULL128 ((U128)0)

/************************************************************************/
// Time values

#define INFINITY 0xFFFFFFFF

/***************************************************************************/
// Some machine constants

#define N_1B ((UINT)0x00000001)
#define N_2B ((UINT)0x00000002)
#define N_4B ((UINT)0x00000004)
#define N_8B ((UINT)0x00000008)
#define N_1KB ((UINT)0x00000400)
#define N_2KB ((UINT)0x00000800)
#define N_4KB ((UINT)0x00001000)
#define N_8KB ((UINT)0x00002000)
#define N_16KB ((UINT)0x00004000)
#define N_32KB ((UINT)0x00008000)
#define N_64KB ((UINT)0x00010000)
#define N_128KB ((UINT)0x00020000)
#define N_256KB ((UINT)0x00040000)
#define N_512KB ((UINT)0x00080000)
#define N_1MB ((UINT)0x00100000)
#define N_2MB ((UINT)0x00200000)
#define N_3MB ((UINT)0x00300000)
#define N_4MB ((UINT)0x00400000)
#define N_8MB ((UINT)0x00800000)
#define N_16MB ((UINT)0x01000000)
#define N_32MB ((UINT)0x02000000)
#define N_64MB ((UINT)0x04000000)
#define N_128MB ((UINT)0x08000000)
#define N_1GB ((UINT)0x40000000)
#define N_2GB ((UINT)0x80000000)
#define N_4GB ((UINT)0xFFFFFFFF)

#define N_1KB_M1 (N_1KB - 1)
#define N_4KB_M1 (N_4KB - 1)
#define N_1MB_M1 (N_1MB - 1)
#define N_4MB_M1 (N_4MB - 1)
#define N_1GB_M1 (N_1GB - 1)
#define N_2GB_M1 (N_2GB - 1)

#ifdef __EXOS_32__
    #define N_HalfMemory (MAX_U32 / 2)
    #define N_FullMemory (MAX_U32)
#endif

#ifdef __EXOS_64__
    #define N_HalfMemory (MAX_U64 / 2)
    #define N_FullMemory (MAX_U64)
#endif

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
// These macros give the offset of a structure member and true if a structure
// of a specified size contains the specified member

#define MEMBER_OFFSET(struc, member) ((UINT)(&(((struc*)NULL)->member)))
#define HAS_MEMBER(struc, member, struc_size) (MEMBER_OFFSET(struc, member) < struc_size)

/***************************************************************************/
// ASCII string types

typedef U8 STR;
typedef const STR CSTR;
typedef STR* LPSTR;
typedef CONST STR* LPCSTR;

#define TEXT(a) ((LPCSTR)a)

/***************************************************************************/
// Unicode string types

typedef U16 USTR;
typedef USTR* LPUSTR;
typedef CONST USTR* LPCUSTR;


/************************************************************************/

#ifdef __KERNEL__
extern void ConsolePrint(LPCSTR Format, ...);
#define CONSOLE_DEBUG(a, ...) { STR __Buf[128]; StringPrintFormat(__Buf, a, ##__VA_ARGS__); ConsolePrint(__Buf); }
#endif

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
#define PATH_SEP STR_SLASH

#define ROOT "/"

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
// Forward declaration to avoid circular dependencies

typedef struct tag_PROCESS PROCESS, *LPPROCESS;

/***************************************************************************/
// A kernel object header

#define OBJECT_FIELDS       \
    U32 TypeID;             \
    U32 References;         \
    U64 ID;                 \
    LPPROCESS OwnerProcess; \

typedef struct tag_OBJECT {
    OBJECT_FIELDS
} OBJECT, *LPOBJECT;

/************************************************************************/
// A datetime

typedef struct tag_DATETIME {
    U32 Year : 22;
    U32 Month : 4;
    U32 Day : 6;
    U32 Hour : 6;
    U32 Minute : 6;
    U32 Second : 6;
    U32 Milli : 10;
    U32 Unused : 4;
} DATETIME, *LPDATETIME;

/************************************************************************/
// Handles - They are a pointer in reality, but called handles so that they
// are not used in userland, otherwise you get a nice privilege violation,
// at best. Will implement pointer masking soon.

typedef UINT HANDLE;
typedef UINT SOCKET_HANDLE;

/************************************************************************/
// Maximum string lengths

#define MAX_STRING_BUFFER 1024
#define MAX_FS_LOGICAL_NAME 64
#define MAX_COMMAND_LINE 1024
#define MAX_PATH_NAME 1024
#define MAX_FILE_NAME 256
#define MAX_USER_NAME 128
#define MAX_NAME 128
#define MAX_PASSWORD 64
#define LOOP_LIMIT 500

/************************************************************************/

#define MAKE_VERSION(maj, min) ((U32)(((((U32)maj) & 0xFFFF) << 16) | (((U32)min) & 0xFFFF)))

#define UNSIGNED(val) *((U32*)(&(val)))
#define SIGNED(val) *((I32*)(&(val)))

/************************************************************************/
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

/************************************************************************/
// 64 bits math

#ifdef __EXOS_32__

// Make U64 from hi/lo
static inline U64 U64_Make(U32 hi, U32 lo) {
    U64 v;
    v.HI = hi;
    v.LO = lo;
    return v;
}

// Add two U64
static inline U64 U64_Add(U64 a, U64 b) {
    U64 r;
    U32 lo = a.LO + b.LO;
    U32 carry = (lo < a.LO) ? 1u : 0u;
    r.LO = lo;
    r.HI = a.HI + b.HI + carry;
    return r;
}

// Subtract b from a
static inline U64 U64_Sub(U64 a, U64 b) {
    U64 r;
    U32 borrow = (a.LO < b.LO) ? 1u : 0u;
    r.LO = a.LO - b.LO;
    r.HI = a.HI - b.HI - borrow;
    return r;
}

// Compare: return -1 if a<b, 0 if a==b, 1 if a>b
static inline int U64_Cmp(U64 a, U64 b) {
    if (a.HI < b.HI) return -1;
    if (a.HI > b.HI) return 1;
    if (a.LO < b.LO) return -1;
    if (a.LO > b.LO) return 1;
    return 0;
}

// Convert U64 to 32-bit if <= 0xFFFFFFFF, else clip
static inline U32 U64_ToU32_Clip(U64 v) {
    if (v.HI != 0) return 0xFFFFFFFFu;
    return v.LO;
}

// Helper functions for U64 operations in CRC64
static inline U64 U64_ShiftRight1(U64 Value) {
    U64 Result;
    Result.LO = (Value.LO >> 1) | ((Value.HI & 1) << 31);
    Result.HI = Value.HI >> 1;
    return Result;
}

static inline U64 U64_Xor(U64 A, U64 B) {
    U64 Result;
    Result.LO = A.LO ^ B.LO;
    Result.HI = A.HI ^ B.HI;
    return Result;
}

static inline BOOL U64_IsOdd(U64 Value) { return (Value.LO & 1) != 0; }

static inline U64 U64_FromU32(U32 Value) {
    U64 Result;
    Result.LO = Value;
    Result.HI = 0;
    return Result;
}

static inline U64 U64_FromUINT(UINT Value) {
    U64 Result;
    Result.LO = Value;
    Result.HI = 0;
    return Result;
}

static inline U64 U64_ShiftRight8(U64 Value) {
    U64 Result;
    Result.LO = (Value.LO >> 8) | ((Value.HI & 0xFF) << 24);
    Result.HI = Value.HI >> 8;
    return Result;
}

static inline U32 U64_High32(U64 Value) {
    return Value.HI;
}

static inline U32 U64_Low32(U64 Value) {
    return Value.LO;
}

#else

// Make U64 from hi/lo
static inline U64 U64_Make(U32 hi, U32 lo) {
    return ((U64)hi << 32) | (U64)lo;
}

// Add two U64
static inline U64 U64_Add(U64 a, U64 b) {
    return a + b;
}

// Subtract b from a
static inline U64 U64_Sub(U64 a, U64 b) {
    return a - b;
}

// Compare: return -1 if a<b, 0 if a==b, 1 if a>b
static inline int U64_Cmp(U64 a, U64 b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

// Convert U64 to 32-bit if <= 0xFFFFFFFF, else clip
static inline U32 U64_ToU32_Clip(U64 v) {
    return (v > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (U32)v;
}

// Helper functions for U64 operations in CRC64
static inline U64 U64_ShiftRight1(U64 Value) {
    return Value >> 1;
}

static inline U64 U64_Xor(U64 A, U64 B) {
    return A ^ B;
}

static inline BOOL U64_IsOdd(U64 Value) { return (Value & 1ull) != 0; }

static inline U64 U64_FromU32(U32 Value) {
    return (U64)Value;
}

static inline U64 U64_FromUINT(UINT Value) {
    return (U64)Value;
}

static inline U64 U64_ShiftRight8(U64 Value) {
    return Value >> 8;
}

static inline U32 U64_High32(U64 Value) {
    return (U32)(Value >> 32);
}

static inline U32 U64_Low32(U64 Value) {
    return (U32)(Value & 0xFFFFFFFFull);
}

#endif

/************************************************************************/

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#endif  // BASE_H_INCLUDED
