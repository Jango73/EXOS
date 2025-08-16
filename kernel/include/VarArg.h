
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef VARARGS_H_INCLUDED
#define VARARGS_H_INCLUDED

/* Use compiler intrinsics for ABI-correct varargs (works en freestanding) */
#if defined(__GNUC__) || defined(__clang__)
    typedef __builtin_va_list VarArgList;
    #define VarArgStart(ap, last_named) __builtin_va_start(ap, last_named)
    #define VarArg(ap, type)            __builtin_va_arg(ap, type)
    #define VarArgEnd(ap)               __builtin_va_end(ap)
#else
    /* Fallback (rare). Keep your original logic if no builtins exist. */
    typedef LPSTR VarArgList[1];
    #define VA_ALIGN(sz) (((sz) + sizeof(int) - 1) & ~(sizeof(int) - 1))
    #define VarArgStart(ap, pn) ((ap)[0] = (LPSTR)&(pn) + VA_ALIGN(sizeof(pn)), (void)0)
    #define VarArg(ap, type)   ((ap)[0] += VA_ALIGN(sizeof(type)), (*(type*)((ap)[0] - VA_ALIGN(sizeof(type)))))
    #define VarArgEnd(ap)      ((ap)[0] = 0, (void)0)
#endif

#endif
