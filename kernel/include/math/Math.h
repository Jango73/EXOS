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


    Math primitives

\************************************************************************/

#ifndef MATH_H_INCLUDED
#define MATH_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#define MATH_PI_F32 3.14159265358979323846f
#define MATH_PI_F64 3.14159265358979323846
#define MATH_TWO_PI_F32 6.28318530717958647692f
#define MATH_TWO_PI_F64 6.28318530717958647692
#define MATH_HALF_PI_F32 1.57079632679489661923f
#define MATH_HALF_PI_F64 1.57079632679489661923
#define MATH_EPSILON_F32 0.000001f
#define MATH_EPSILON_F64 0.000000000001

/************************************************************************/

BOOL MathHasHardwareFPU(void);
F32 MathSinF32(F32 Radians);
F32 MathCosF32(F32 Radians);
F64 MathSinF64(F64 Radians);
F64 MathCosF64(F64 Radians);
F32 MathSqrtF32(F32 Value);
F64 MathSqrtF64(F64 Value);

/************************************************************************/

#endif
