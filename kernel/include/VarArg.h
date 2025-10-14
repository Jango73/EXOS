
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

#include "Base.h"

#ifdef __EXOS_32__

typedef LPSTR VarArgList[1];
#define VA_ALIGN(sz) (((sz) + sizeof(INT) - 1) & ~(sizeof(INT) - 1))
#define VarArgStart(ap, pn) ((ap)[0] = (LPSTR) & (pn) + VA_ALIGN(sizeof(pn)), (void)0)
#define VarArg(ap, type) ((ap)[0] += VA_ALIGN(sizeof(type)), (*(type*)((ap)[0] - VA_ALIGN(sizeof(type)))))
#define VarArgEnd(ap) ((ap)[0] = 0, (void)0)

#else

/* System V AMD64 vararg list */
typedef struct {
    U32 gp_offset;         /* 0..48 step 8 (RDI..R9) */
    U32 fp_offset;         /* 48..176 step 16 (XMM0..7) */
    LPVOID overflow_arg_area; /* stack overflow area */
    LPVOID reg_save_area;     /* base of saved regs */
} VarArgList;

/* ABI constants */
enum {
    GP_BASE  = 0,
    GP_LIMIT = 48,   /* 6 × 8 bytes */
    FP_BASE  = 48,
    FP_LIMIT = 176   /* 48 + 8 × 16 */
};

/* Initializes VarArgList after prologue (regs already saved). */
static inline void VarArgStart(VarArgList* ap, LPVOID reg_save_area,
                               LPVOID overflow_arg_area, U32 gp_initial, U32 fp_initial) {
    ap->gp_offset = gp_initial;
    ap->fp_offset = fp_initial;
    ap->overflow_arg_area = overflow_arg_area;
    ap->reg_save_area = reg_save_area;
}

/* Ends a VarArgList (no-op for symmetry) */
static inline void VarArgEnd(VarArgList* ap) {
    UNUSED(ap);
}

/* Fetch 64-bit integer-like argument (includes pointers) */
static inline U64 VarArgU64(VarArgList* ap) {
    U64 v;
    if (ap->gp_offset < GP_LIMIT) {
        v = *(U64*)((U8*)ap->reg_save_area + GP_BASE + ap->gp_offset);
        ap->gp_offset += 8;
    } else {
        v = *(U64*)(ap->overflow_arg_area);
        ap->overflow_arg_area = (U8*)ap->overflow_arg_area + 8;
    }
    return v;
}

/* Signed / pointer / float helpers */
static inline I64 VarArgI64(VarArgList* ap) { return (I64)VarArgU64(ap); }
static inline LPVOID VarArgPtr(VarArgList* ap) { return (LPVOID)(U64)VarArgU64(ap); }

/* Fetch 64-bit floating argument (float promoted → double → F64) */
static inline F64 VarArgF64(VarArgList* ap) {
    F64 v;
    if (ap->fp_offset < FP_LIMIT) {
        v = *(F64*)((U8*)ap->reg_save_area + ap->fp_offset);
        ap->fp_offset += 16;
    } else {
        v = *(F64*)(ap->overflow_arg_area);
        ap->overflow_arg_area = (U8*)ap->overflow_arg_area + 8;
    }
    return v;
}

/* Generic accessor */
#define VarArg(AP, TYPE) \
    _Generic((TYPE)0, \
        F64: VarArgF64, \
        default: VarArgU64 \
    )(AP)

#endif  // __EXOS_32__

#endif  // VARARGS_H_INCLUDED
