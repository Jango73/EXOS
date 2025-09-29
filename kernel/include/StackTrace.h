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

    Stack tracing facility

\************************************************************************/

#ifndef STACKTRACE_H_INCLUDED
#define STACKTRACE_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "Log.h"

/************************************************************************/

#define STACK_TRACE_WARNING 256

/************************************************************************/

// Prologue
// DOES NOT modify the stack, reads ESP
#if TRACE_STACK_USAGE == 1
#define TRACED_FUNCTION   \
    LINEAR __stack_start; \
    __asm__ __volatile__("movl %%esp, %0\n\t" : "=r"(__stack_start) : :)
#else
#define TRACED_FUNCTION
#endif

/************************************************************************/

// Epilogue
// DOES NOT modify the stack, reads ESP
#if TRACE_STACK_USAGE == 1
#if SCHEDULING_DEBUG_OUTPUT == 1
#define TRACED_EPILOGUE(func_name)                                                      \
    LINEAR __stack_end;                                                                 \
    __asm__ __volatile__("movl %%esp, %0\n\t" : "=r"(__stack_end) : :);                 \
    LINEAR __stack_used = __stack_start - __stack_end;                                  \
    DEBUG(TEXT("ESP in " #func_name " = %x"), __stack_end);                             \
    if (__stack_used > STACK_TRACE_WARNING) {                                           \
        WARNING(TEXT("Stack usage exceeds limit (%x) in " #func_name), __stack_used);   \
    }
#else
#define TRACED_EPILOGUE(func_name)                                                      \
    LINEAR __stack_end;                                                                 \
    __asm__ __volatile__("movl %%esp, %0\n\t" : "=r"(__stack_end) : :);                 \
    LINEAR __stack_used = __stack_start - __stack_end;                                  \
    if (__stack_used > STACK_TRACE_WARNING) {                                           \
        WARNING(TEXT("Stack usage exceeds limit (%x) in " #func_name), __stack_used);   \
    }
#endif
#else
#define TRACED_EPILOGUE(func_name)
#endif

/************************************************************************/

#endif
