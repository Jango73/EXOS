
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


    VarArg

\************************************************************************/

#ifndef VARARGS_H_INCLUDED
#define VARARGS_H_INCLUDED

// x86-64 builds rely on the compiler-provided vararg intrinsics to properly
// handle register and stack based arguments following the System V ABI.
#if defined(__x86_64__) || defined(__amd64__)
typedef __builtin_va_list VarArgList;
#define VarArgStart(ap, pn) __builtin_va_start(ap, pn)
#define VarArg(ap, type) __builtin_va_arg(ap, type)
#define VarArgEnd(ap) __builtin_va_end(ap)
#else
typedef LPSTR VarArgList[1];
#define VA_ALIGN(sz) (((sz) + sizeof(int) - 1) & ~(sizeof(int) - 1))
#define VarArgStart(ap, pn) ((ap)[0] = (LPSTR) & (pn) + VA_ALIGN(sizeof(pn)), (void)0)
#define VarArg(ap, type) ((ap)[0] += VA_ALIGN(sizeof(type)), (*(type*)((ap)[0] - VA_ALIGN(sizeof(type)))))
#define VarArgEnd(ap) ((ap)[0] = 0, (void)0)
#endif

#endif
