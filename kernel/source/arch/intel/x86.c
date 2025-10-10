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


    Intel common helpers

\************************************************************************/

#include "arch/Disassemble.h"
#include "Memory.h"
#include "String.h"
#include "arch/intel/i386-Asm.h"

/***************************************************************************/

static void SetDisassemblyAttributes(U32 NumBits) {
    switch (NumBits) {
        case 16:
            SetIntelAttributes(I16BIT, I16BIT);
            break;
        case 64:
            SetIntelAttributes(I64BIT, I64BIT);
            break;
        case 32:
        default:
            SetIntelAttributes(I32BIT, I32BIT);
            break;
    }
}

/***************************************************************************/

void Disassemble(LPSTR Buffer, LINEAR InstructionPointer, U32 NumInstructions, U32 NumBits) {
    STR LineBuffer[128];
    STR DisasmBuffer[64];
    STR HexBuffer[64];

    Buffer[0] = STR_NULL;

    U8* BasePtr = (U8*)VMA_USER;
    U8* CodePtr = (U8*)InstructionPointer;

    if (InstructionPointer >= VMA_LIBRARY) BasePtr = (U8*)VMA_LIBRARY;
    if (InstructionPointer >= VMA_KERNEL) BasePtr = (U8*)VMA_KERNEL;

    if (IsValidMemory(InstructionPointer) && IsValidMemory(InstructionPointer + NumInstructions - 1)) {
        SetDisassemblyAttributes(NumBits);

        for (U32 Index = 0; Index < NumInstructions; Index++) {
            U32 InstrLength = Intel_MachineCodeToString((LPCSTR)BasePtr, (LPCSTR)CodePtr, DisasmBuffer);

            if (InstrLength > 0 && InstrLength <= 20) {
                StringPrintFormat(HexBuffer, TEXT("%x: "), CodePtr);

                for (U32 ByteIndex = 0; ByteIndex < InstrLength && ByteIndex < 8; ByteIndex++) {
                    STR ByteHex[24];
                    StringPrintFormat(ByteHex, TEXT("%x "), CodePtr[ByteIndex]);
                    StringConcat(HexBuffer, ByteHex);
                }

                while (StringLength(HexBuffer) < 40) {
                    StringConcat(HexBuffer, TEXT(" "));
                }

                StringPrintFormat(LineBuffer, TEXT("%s %s\n"), HexBuffer, DisasmBuffer);
                StringConcat(Buffer, LineBuffer);

                CodePtr += InstrLength;
            } else {
                break;
            }
        }
    } else {
        StringPrintFormat(LineBuffer, TEXT("Can't disassemble at %x (base %x)\n"), CodePtr, BasePtr);
        StringConcat(Buffer, LineBuffer);
    }
}

/***************************************************************************/
