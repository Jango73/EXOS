
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


    i386 Machine code instructions

\************************************************************************/

#include "../../include/arch/i386/I386-MCI.h"

#include "../../include/Heap.h"
#include "../../include/String.h"
#include "../../include/System.h"

/*************************************************************************************************/

static long IntelOperandSize = I32BIT;
static long IntelAddressSize = I32BIT;

LPCSTR Intel_RegNames[] = {
    TEXT(""),    TEXT("AL"),  TEXT("CL"),  TEXT("DL"),  TEXT("BL"),  TEXT("AH"),  TEXT("CH"),  TEXT("DH"),  TEXT("BH"),
    TEXT("AX"),  TEXT("CX"),  TEXT("DX"),  TEXT("BX"),  TEXT("SP"),  TEXT("BP"),  TEXT("SI"),  TEXT("DI"),  TEXT("EAX"),
    TEXT("ECX"), TEXT("EDX"), TEXT("EBX"), TEXT("ESP"), TEXT("EBP"), TEXT("ESI"), TEXT("EDI"), TEXT("MM0"), TEXT("MM1"),
    TEXT("MM2"), TEXT("MM3"), TEXT("MM4"), TEXT("MM5"), TEXT("MM6"), TEXT("MM7"), TEXT("ES"),  TEXT("CS"),  TEXT("SS"),
    TEXT("DS"),  TEXT("FS"),  TEXT("GS"),  TEXT("CR0"), TEXT("CR2"), TEXT("CR3"), TEXT("CR4")};

/*************************************************************************************************/

#define INTEL_MODRM_R_M_SHFT (0)
#define INTEL_MODRM_R_M_MASK (0x07 << INTEL_MODRM_R_M_SHFT)

#define INTEL_MODRM_REG_SHFT (3)
#define INTEL_MODRM_REG_MASK (0x07 << INTEL_MODRM_REG_SHFT)

#define INTEL_MODRM_MOD_SHFT (6)
#define INTEL_MODRM_MOD_MASK (0x03 << INTEL_MODRM_MOD_SHFT)

/*************************************************************************************************/

#define INTEL_SIB_BASE_SHFT (0)
#define INTEL_SIB_BASE_MASK (0x07 << INTEL_SIB_BASE_SHFT)

#define INTEL_SIB_INDX_SHFT (3)
#define INTEL_SIB_INDX_MASK (0x07 << INTEL_SIB_INDX_SHFT)

#define INTEL_SIB_SCAL_SHFT (6)
#define INTEL_SIB_SCAL_MASK (0x03 << INTEL_SIB_SCAL_SHFT)

/*************************************************************************************************/

/**
 * @brief Get the size in bits of a register.
 * @param Reg Register identifier (INTEL_REG_* constants).
 * @return Size in bits (I8BIT, I16BIT, I32BIT, I64BIT) or 0 if invalid.
 */
U32 Intel_GetRegisterSize(U32 Reg) {
    if (Reg >= INTEL_REG_AL && Reg <= INTEL_REG_BH) return I8BIT;
    if (Reg >= INTEL_REG_AX && Reg <= INTEL_REG_DI) return I16BIT;
    if (Reg >= INTEL_REG_EAX && Reg <= INTEL_REG_EDI) return I32BIT;
    if (Reg >= INTEL_REG_MM0 && Reg <= INTEL_REG_MM7) return I64BIT;
    if (Reg >= INTEL_REG_ES && Reg <= INTEL_REG_GS) return I16BIT;

    return 0;
}

/*************************************************************************************************/

/**
 * @brief Get the operand size from instruction and operand prototype.
 * @param Instruction Pointer to instruction structure.
 * @param Prototype Operand prototype string (e.g., "Eb", "Gv").
 * @return Size in bits (I8BIT, I16BIT, I32BIT, I64BIT) or 0.
 */
U32 Intel_GetOperandSize(LPINTEL_INSTRUCTION Instruction, LPCSTR Prototype) {
    long c = 0;

    if (Prototype[0] == '_') {
        long Reg = 0;

        for (c = 0; c < INTEL_REG_LAST; c++) {
            if (STRINGS_EQUAL(Intel_RegNames[c], Prototype + 1)) {
                Reg = c;
                break;
            }
        }

        // Check the operand size
        if (Reg >= INTEL_REG_AX && Reg <= INTEL_REG_DI) {
            if (Instruction->OperandSize == I32BIT) Reg = INTEL_REG_32 + (Reg - INTEL_REG_16);
        }

        // Return the size of the register
        return Intel_GetRegisterSize(Reg);
    }

    switch (Prototype[1]) {
        case 'b':
            return I8BIT;
        case 'w':
            return I16BIT;
        case 'd':
            return I32BIT;
        case 'q':
            return I64BIT;
        case 'c':
            if (Instruction->OperandSize == I16BIT) return I16BIT;
            return I8BIT;
        case 'v':
            if (Instruction->OperandSize == I32BIT) return I32BIT;
            return I16BIT;
    }

    return 0;  // Make the compiler happy :)
}

/*************************************************************************************************/

/**
 * @brief Get the address size from instruction and prototype.
 * @param Instruction Pointer to instruction structure.
 * @param Prototype Prototype string.
 * @return Size in bits (I16BIT, I32BIT) or 0.
 */
U32 Intel_GetAddressSize(LPINTEL_INSTRUCTION Instruction, LPSTR Prototype) {
    switch (Prototype[1]) {
        case 'w':
            return I16BIT;
        case 'v':
            if (Instruction->AddressSize == I32BIT) return I32BIT;
            return I16BIT;
        case 'd':
            return I32BIT;
    }

    return 0;  // Make the compiler happy :)
}

/*************************************************************************************************/

/**
 * @brief Map a size in bits to an array index.
 * @param Size Size in bits (I8BIT, I16BIT, I32BIT, I64BIT).
 * @return Index (0-3) for the given size.
 */
U32 Intel_MapSizeToIndex(U32 Size) {
    switch (Size) {
        case I8BIT:
            return 0;
        case I16BIT:
            return 1;
        case I32BIT:
            return 2;
        case I64BIT:
            return 3;
    }

    return 0;  // Make the compiler happy :)
}

/*************************************************************************************************/

/**
 * @brief Extract ModR/M byte from instruction buffer.
 * @param Instruction Pointer to instruction structure to fill.
 * @param InstBuffer Pointer to instruction bytes.
 * @return Number of bytes consumed (always 1).
 */
U32 Intel_GetModR_M(LPINTEL_INSTRUCTION Instruction, U8* InstBuffer) {
    Instruction->ModR_M.Byte = *InstBuffer;

    return sizeof(U8);
}

/*************************************************************************************************/

/**
 * @brief Extract SIB (Scale-Index-Base) byte from instruction buffer.
 * @param Instruction Pointer to instruction structure to fill.
 * @param InstBuffer Pointer to instruction bytes.
 * @return Number of bytes consumed (always 1).
 */
U32 Intel_GetSIB(LPINTEL_INSTRUCTION Instruction, U8* InstBuffer) {
    Instruction->SIB.Byte = *InstBuffer;

    return sizeof(U8);
}

/*************************************************************************************************/

/**
 * @brief Decode Scale-Index-Base (SIB) byte for 32-bit addressing.
 * @param Instruction Pointer to instruction structure to update.
 * @param Operand Pointer to operand structure to fill.
 * @param Prototype Operand prototype string.
 * @param InstBuffer Pointer to instruction buffer.
 * @return Number of bytes processed.
 */
U32 Intel_Decode_SIB(LPINTEL_INSTRUCTION Instruction, LPINTEL_OPERAND Operand, LPCSTR Prototype, U8* InstBuffer) {
    UNUSED(Prototype);

    Intel_GetSIB(Instruction, InstBuffer);

    Operand->BISD.Base = INTEL_REG_32 + Instruction->SIB.Bits.Base;

    switch (Instruction->SIB.Bits.Index) {
        case 0:
            Operand->BISD.Index = INTEL_REG_EAX;
            break;
        case 1:
            Operand->BISD.Index = INTEL_REG_ECX;
            break;
        case 2:
            Operand->BISD.Index = INTEL_REG_EDX;
            break;
        case 3:
            Operand->BISD.Index = INTEL_REG_EBX;
            break;
        case 4:
            Operand->BISD.Index = 0;
            break;
        case 5:
            Operand->BISD.Index = INTEL_REG_EBP;
            break;
        case 6:
            Operand->BISD.Index = INTEL_REG_ESI;
            break;
        case 7:
            Operand->BISD.Index = INTEL_REG_EDI;
            break;
    }

    switch (Instruction->SIB.Bits.Scale) {
        case 0:
            Operand->BISD.Scale = 0x01;
            break;
        case 1:
            Operand->BISD.Scale = 0x02;
            break;
        case 2:
            Operand->BISD.Scale = 0x04;
            break;
        case 3:
            Operand->BISD.Scale = 0x08;
            break;
    }

    if (Operand->BISD.Base == INTEL_REG_EBP && Instruction->ModR_M.Bits.Mod == 0x00) {
        Operand->BISD.Base = 0;
    }

    return sizeof(U8);
}

/*************************************************************************************************/

/**
 * @brief Decode ModR/M byte for 32-bit addressing modes.
 * @param Instruction Pointer to instruction structure to update.
 * @param Operand Pointer to operand structure to fill.
 * @param Prototype Operand prototype string.
 * @param InstBuffer Pointer to instruction buffer.
 * @return Number of bytes processed.
 */
U32 Intel_Decode_ModRM_Addressing_32(
    LPINTEL_INSTRUCTION Instruction, LPINTEL_OPERAND Operand, LPCSTR Prototype, U8* InstBuffer) {
    U8* InstPtr = InstBuffer;

    //-------------------------------------
    // Check the special case of the single 32-bit displacement
    if (Instruction->ModR_M.Bits.Mod == 0x00 && Instruction->ModR_M.Bits.R_M == 0x05) {
        Operand->DSP.Type = INTEL_OPERAND_TYPE_DSP;
        Operand->DSP.Size = I32BIT;
        Operand->DSP.Value = *((I32*)InstPtr);
        InstPtr += sizeof(I32);
        return InstPtr - InstBuffer;
    }

    //-------------------------------------
    // Check if we have a single register
    if (Instruction->ModR_M.Bits.Mod == 0x03) {
        // When Mod == 0x03, we can have any general register : AL..BH, AX..DI, EAX..EDI, MM0..MM7
        long Register = INTEL_REG_8 + (Intel_MapSizeToIndex(Operand->Any.Size) * 0x08) + Instruction->ModR_M.Bits.R_M;
        Operand->R.Type = INTEL_OPERAND_TYPE_R;
        Operand->R.Size = Intel_GetRegisterSize(Register);
        Operand->R.Register = Register;
        return InstPtr - InstBuffer;
    }

    Operand->BISD.Type = INTEL_OPERAND_TYPE_BISD;
    Operand->BISD.Size = Operand->Any.Size;

    //-------------------------------------
    // Add the registers involved in the indirect addressing
    switch (Instruction->ModR_M.Bits.R_M) {
        case 0x00:
            Operand->BISD.Base = INTEL_REG_EAX;
            break;
        case 0x01:
            Operand->BISD.Base = INTEL_REG_ECX;
            break;
        case 0x02:
            Operand->BISD.Base = INTEL_REG_EDX;
            break;
        case 0x03:
            Operand->BISD.Base = INTEL_REG_EBX;
            break;
        case 0x05:
            Operand->BISD.Base = INTEL_REG_EBP;
            break;
        case 0x06:
            Operand->BISD.Base = INTEL_REG_ESI;
            break;
        case 0x07:
            Operand->BISD.Base = INTEL_REG_EDI;
            break;
        case 0x04:
            InstPtr += Intel_Decode_SIB(Instruction, Operand, Prototype, InstPtr);
            break;
    }

    //-------------------------------------
    // Check if we have a displacement value
    if (Instruction->ModR_M.Bits.Mod == 0x01) {
        Operand->BISD.Displace = *((I8*)InstPtr);  // ChangÃ© de U8 Ã  I8 pour gÃ©rer le signe
        InstPtr += sizeof(I8);
    }
    if (Instruction->ModR_M.Bits.Mod == 0x02) {
        Operand->BISD.Displace = *((I32*)InstPtr);
        InstPtr += sizeof(I32);
    }

    return InstPtr - InstBuffer;
}

/*************************************************************************************************/

/**
 * @brief Decode ModR/M byte for 16-bit addressing modes.
 * @param Instruction Pointer to instruction structure to update.
 * @param Operand Pointer to operand structure to fill.
 * @param Prototype Operand prototype string.
 * @param InstBuffer Pointer to instruction buffer.
 * @return Number of bytes processed.
 */
U32 Intel_Decode_ModRM_Addressing_16(
    LPINTEL_INSTRUCTION Instruction, LPINTEL_OPERAND Operand, LPCSTR Prototype, U8* InstBuffer) {
    UNUSED(Prototype);

    U8* InstPtr = InstBuffer;

    if (Instruction->ModR_M.Bits.Mod == 0x03) {
        // When Mod == 0x03, we can have any general register : AL..BH, AX..DI, EAX..EDI, MM0..MM7
        U32 Register =
            INTEL_REG_8 + (Intel_MapSizeToIndex(Instruction->OperandSize) * 0x08) + Instruction->ModR_M.Bits.R_M;

        Operand->R.Type = INTEL_OPERAND_TYPE_R;
        Operand->R.Size = Intel_GetRegisterSize(Register);
        Operand->R.Register = Register;

        return InstPtr - InstBuffer;
    }

    Operand->BISD.Type = INTEL_OPERAND_TYPE_BISD;
    Operand->BISD.Size = Operand->Any.Size;

    //-------------------------------------
    // We have a special case when Mod == 0x00 and R/M == 0x06 where
    // instead of [BP+disp16] we just have [disp16]

    if (Instruction->ModR_M.Bits.Mod == 0x00 && Instruction->ModR_M.Bits.R_M == 0x06) {
        Operand->BISD.Displace = *((U16*)InstPtr);
        InstPtr += sizeof(U16);
    }

    if (Instruction->ModR_M.Bits.Mod == 0x01) {
        Operand->BISD.Displace = *((U8*)InstPtr);
        InstPtr += sizeof(U8);
    }
    if (Instruction->ModR_M.Bits.Mod == 0x02) {
        Operand->BISD.Displace = *((U16*)InstPtr);
        InstPtr += sizeof(U16);
    }

    //-------------------------------------
    // Add the registers involved in the indirect addressing

    switch (Instruction->ModR_M.Bits.R_M) {
        case 0x00:
            Operand->BISD.Base = INTEL_REG_BX;
            Operand->BISD.Index = INTEL_REG_SI;
            break;
        case 0x01:
            Operand->BISD.Base = INTEL_REG_BX;
            Operand->BISD.Index = INTEL_REG_DI;
            break;
        case 0x02:
            Operand->BISD.Base = INTEL_REG_BP;
            Operand->BISD.Index = INTEL_REG_SI;
            break;
        case 0x03:
            Operand->BISD.Base = INTEL_REG_BP;
            Operand->BISD.Index = INTEL_REG_DI;
            break;
        case 0x04:
            Operand->BISD.Base = INTEL_REG_SI;
            break;
        case 0x05:
            Operand->BISD.Base = INTEL_REG_DI;
            break;
        case 0x07:
            Operand->BISD.Base = INTEL_REG_BX;
            break;
        case 0x06:
            if (Instruction->ModR_M.Bits.Mod != 0x00) Operand->BISD.Base = INTEL_REG_BP;
            break;
    }

    return InstPtr - InstBuffer;
}

/*************************************************************************************************/

/**
 * @brief Decode ModR/M addressing based on address size (16-bit or 32-bit).
 * @param Instruction Pointer to instruction structure to update.
 * @param Operand Pointer to operand structure to fill.
 * @param Prototype Operand prototype string.
 * @param InstBuffer Pointer to instruction buffer.
 * @return Number of bytes processed.
 */
U32 Intel_Decode_ModRM_Addressing(
    LPINTEL_INSTRUCTION Instruction, LPINTEL_OPERAND Operand, LPCSTR Prototype, U8* InstBuffer) {
    if (Instruction->AddressSize == I16BIT) {
        return Intel_Decode_ModRM_Addressing_16(Instruction, Operand, Prototype, InstBuffer);
    }

    return Intel_Decode_ModRM_Addressing_32(Instruction, Operand, Prototype, InstBuffer);
}

/*************************************************************************************************/

/**
 * @brief Decode a single operand based on its prototype string.
 * @param Instruction Pointer to instruction structure to update.
 * @param Operand Pointer to operand structure to fill.
 * @param Prototype Operand prototype string (e.g., "Eb", "Gv", "Ib").
 * @param InstBuffer Pointer to instruction buffer.
 * @return Number of bytes processed.
 */
U32 Intel_Decode_Operand(LPINTEL_INSTRUCTION Instruction, LPINTEL_OPERAND Operand, LPCSTR Prototype, U8* InstBuffer) {
    U8* InstPtr = (U8*)InstBuffer;
    U32 DirectReg = 0;
    long c = 0;

    if (StringCompare(Prototype, TEXT("")) == 0) return (U32)0;

    // Check if there is a direct register or number
    if (Prototype[0] == '_') {
        LPCSTR RegName = Prototype + 1;

        for (c = 0; c < INTEL_REG_LAST; c++) {
            if (STRINGS_EQUAL(RegName, Intel_RegNames[c])) {
                DirectReg = c;
                break;
            }
        }

        if (DirectReg != 0) {
            // Check the operand size
            if (DirectReg >= INTEL_REG_AX && DirectReg <= INTEL_REG_DI && Instruction->OperandSize == I32BIT) {
                DirectReg = INTEL_REG_32 + (DirectReg - INTEL_REG_16);
            }

            // Set it in the instruction
            Operand->R.Type = INTEL_OPERAND_TYPE_R;
            Operand->R.Size = Intel_GetRegisterSize(DirectReg);
            Operand->R.Register = DirectReg;
        } else {
            // We assume it is a direct number or anything else (for example : INT 3)
            Operand->STR.Type = INTEL_OPERAND_TYPE_STR;
            Operand->STR.Size = 0;
            StringCopy(Operand->STR.String, RegName);
        }
    } else
        switch (Prototype[0]) {
            case 'A': {
                // We have a 32-bit or 48-bit pointer
                U32 Offset = 0;
                U32 Segment = 0;

                // Get the offset
                switch (Instruction->AddressSize) {
                    case I16BIT:
                        Offset = *((U16*)InstPtr);
                        InstPtr += sizeof(U16);
                        break;
                    case I32BIT:
                        Offset = *((U32*)InstPtr);
                        InstPtr += sizeof(U32);
                        break;
                }

                // Get the segment
                Segment = *((U16*)InstPtr);
                InstPtr += sizeof(U16);

                // Assign values to the structure
                switch (Instruction->AddressSize) {
                    case I16BIT: {
                        Operand->SO16.Type = INTEL_OPERAND_TYPE_SO16;
                        Operand->SO16.Size = I32BIT;
                        Operand->SO16.Segment = Segment;
                        Operand->SO16.Offset = Offset;
                    } break;
                    case I32BIT: {
                        Operand->SO32.Type = INTEL_OPERAND_TYPE_SO32;
                        Operand->SO32.Size = I48BIT;
                        Operand->SO32.Segment = Segment;
                        Operand->SO32.Offset = Offset;
                    } break;
                }
            } break;

            case 'C': {
            } break;

            case 'D': {
            } break;

            case 'E':
            case 'M': {
                InstPtr += Intel_Decode_ModRM_Addressing(Instruction, Operand, Prototype, InstPtr);
            } break;

            case 'F': {
            } break;

            case 'G': {
                // The general purpose register is in the REG field of the ModR/M byte
                long Register =
                    INTEL_REG_8 + (Intel_MapSizeToIndex(Operand->Any.Size) * 0x08) + Instruction->ModR_M.Bits.Reg;
                Operand->R.Type = INTEL_OPERAND_TYPE_R;
                Operand->R.Size = Intel_GetRegisterSize(Register);
                Operand->R.Register = Register;
            } break;

            case 'I': {
                switch (Operand->Any.Size) {
                    case I8BIT: {
                        Operand->I8.Type = INTEL_OPERAND_TYPE_I8;
                        Operand->I8.Size = I8BIT;
                        Operand->I8.Value = *((U8*)InstPtr);
                        InstPtr += sizeof(U8);
                    } break;
                    case I16BIT: {
                        Operand->I16.Type = INTEL_OPERAND_TYPE_I16;
                        Operand->I16.Size = I16BIT;
                        Operand->I16.Value = *((U16*)InstPtr);
                        InstPtr += sizeof(U16);
                    } break;
                    case I32BIT: {
                        Operand->I32.Type = INTEL_OPERAND_TYPE_I32;
                        Operand->I32.Size = I32BIT;
                        Operand->I32.Value = *((U32*)InstPtr);
                        InstPtr += sizeof(U32);
                    } break;
                }
            } break;

            case 'J': {
                switch (Operand->Any.Size) {
                    case I8BIT: {
                        Operand->DSP.Type = INTEL_OPERAND_TYPE_DSP;
                        Operand->DSP.Size = I8BIT;
                        Operand->DSP.Value = *((I8*)InstPtr);
                        InstPtr += sizeof(I8);
                    } break;
                    case I16BIT: {
                        Operand->DSP.Type = INTEL_OPERAND_TYPE_DSP;
                        Operand->DSP.Size = I16BIT;
                        Operand->DSP.Value = *((I16*)InstPtr);
                        InstPtr += sizeof(I16);
                    } break;
                    case I32BIT: {
                        Operand->DSP.Type = INTEL_OPERAND_TYPE_DSP;
                        Operand->DSP.Size = I32BIT;
                        Operand->DSP.Value = *((I32*)InstPtr);
                        InstPtr += sizeof(I32);
                    } break;
                }
            } break;

            case 'O': {
                // This is an indirect addressing with an immediate operand
                switch (Instruction->AddressSize) {
                    case I16BIT: {
                        Operand->II.Type = INTEL_OPERAND_TYPE_II;
                        Operand->II.Size = I16BIT;
                        Operand->II.Value = *((I16*)InstPtr);
                        InstPtr += sizeof(I16);
                    } break;
                    case I32BIT: {
                        Operand->II.Type = INTEL_OPERAND_TYPE_II;
                        Operand->II.Size = I32BIT;
                        Operand->II.Value = *((I32*)InstPtr);
                        InstPtr += sizeof(I32);
                    } break;
                }
            } break;

            case 'P': {
                // The Reg field of the ModR/M byte is a MMX register
                Operand->R.Type = INTEL_OPERAND_TYPE_R;
                Operand->R.Size = I64BIT;
                Operand->R.Register = INTEL_REG_64 + Instruction->ModR_M.Bits.Mod;
            } break;

            case 'Q': {
            } break;

            case 'R': {
                // The general purpose register is in the Mod field of the ModR/M byte
                long Register =
                    INTEL_REG_8 + (Intel_MapSizeToIndex(Operand->Any.Size) * 0x08) + Instruction->ModR_M.Bits.Mod;
                Operand->R.Type = INTEL_OPERAND_TYPE_R;
                Operand->R.Size = Intel_GetRegisterSize(Register);
                Operand->R.Register = Register;
            } break;

            case 'S': {
                // The Reg field of the ModR/M byte is a Segment register
                Operand->R.Type = INTEL_OPERAND_TYPE_R;
                Operand->R.Size = I16BIT;
                Operand->R.Register = INTEL_REG_SEG + Instruction->ModR_M.Bits.Reg;
            } break;

            case 'T': {
                // The Reg field of the ModR/M byte is a Test register
            } break;

            case 'X': {
                // StringConcat(InstString, "DS:[SI]");
            } break;

            case 'Y': {
                // StringConcat(InstString, "ES:[DI]");
            } break;
        }

    return InstPtr - InstBuffer;
}

/*************************************************************************************************/

/**
 * @brief Check if an operand prototype code requires ModR/M byte.
 * @param Code Operand prototype character (e.g., 'C', 'D', 'E', 'G', etc.).
 * @return 1 if ModR/M byte is required, 0 otherwise.
 */
int Intel_IsModR_M(U8 Code) {
    switch (Code) {
        case 'C':
        case 'D':
        case 'E':
        case 'G':
        case 'M':
        case 'P':
        case 'Q':
        case 'R':
        case 'S':
        case 'T':
            return 1;
    }

    return 0;
}

/*************************************************************************************************/

/**
 * @brief Decode machine code bytes into an instruction structure.
 * @param Base Base address for relative addressing calculations.
 * @param InstBuffer Pointer to instruction bytes to decode.
 * @param Instruction Pointer to instruction structure to fill.
 * @return Length of decoded instruction in bytes.
 */
U32 Intel_MachineCodeToStructure(LPCSTR Base, LPCSTR InstBuffer, LPINTEL_INSTRUCTION Instruction) {
    INTEL_OPCODE_PROTOTYPE* OpTblPtr = NULL;

    U8* InstPtr = (U8*)InstBuffer;

    LPCSTR OpProto1 = NULL;
    LPCSTR OpProto2 = NULL;
    LPCSTR OpProto3 = NULL;

    U32 Op2b = 0;
    U32 Extension = 0;
    U32 HaveModR_M = 0;
    U32 OpcodeRow = 0;
    U32 OpcodeCol = 0;
    U32 OpcodeIndex = 0;

    //-------------------------------------
    // Clear the instruction structure

    MemorySet(Instruction, 0, sizeof(INTEL_INSTRUCTION));

    //-------------------------------------
    // Set the base and address of the opcode

    Instruction->Base = (U8*)Base;
    Instruction->Address = (U32)(((U8*)InstBuffer) - ((U8*)Base));

    //-------------------------------------
    // Set the default operand and address sizes

    Instruction->OperandSize = IntelOperandSize;
    Instruction->AddressSize = IntelAddressSize;

    //-------------------------------------
    // Get the starting opcode

    Instruction->Opcode = *InstPtr++;

    //-------------------------------------
    // Check if we have an operand size override prefix

    if (Instruction->Opcode == (U8)0x66) {
        switch (Instruction->OperandSize) {
            case I16BIT:
                Instruction->OperandSize = I32BIT;
                break;
            case I32BIT:
                Instruction->OperandSize = I16BIT;
                break;
        }
        Instruction->Opcode = *InstPtr++;
    }

    //-------------------------------------
    // Check if we have an address size override prefix

    if (Instruction->Opcode == (U8)0x67) {
        switch (Instruction->AddressSize) {
            case I16BIT:
                Instruction->AddressSize = I32BIT;
                break;
            case I32BIT:
                Instruction->AddressSize = I16BIT;
                break;
        }
        Instruction->Opcode = *InstPtr++;
    }

    //-------------------------------------
    // Check if this is a two-byte opcode

    if (Instruction->Opcode == (U8)0x0F) {
        Instruction->Opcode = *InstPtr++;
        Op2b = 1;
    }

    //-------------------------------------
    // Compute the index to the opcode table

    OpcodeRow = (Instruction->Opcode & (U8)0xF0) >> 4;
    OpcodeCol = (Instruction->Opcode & (U8)0x0F) >> 0;
    OpcodeIndex = (Op2b * 0x100) + (OpcodeRow * 0x10) + OpcodeCol;

    //-------------------------------------
    // Assign a pointer to the prototype instruction and its operands

    OpTblPtr = Opcode_Table + OpcodeIndex;
    OpProto1 = OpTblPtr->Operand[0];
    OpProto2 = OpTblPtr->Operand[1];
    OpProto3 = OpTblPtr->Operand[2];

    //-------------------------------------
    // Check if this is an extension
    // If so, redirect the pointer to the extension table

    if (OpTblPtr->Name[0] == 'X' && OpTblPtr->Name[1] == 'G') {
        Extension = StringToI32(OpTblPtr->Name + 2) - 1;
        InstPtr += Intel_GetModR_M(Instruction, InstPtr);
        HaveModR_M = 1;

        OpcodeIndex = (Extension * 0x08) + Instruction->ModR_M.Bits.Reg;
        OpTblPtr = Extension_Table + OpcodeIndex;

        //-------------------------------------
        // Append the operands of the extension group
        // to the ones of the main opcode
        if (OpTblPtr->Operand[0][0]) {
            if (OpProto1[0] == '\0')
                OpProto1 = OpTblPtr->Operand[0];
            else if (OpProto2[0] == '\0')
                OpProto2 = OpTblPtr->Operand[0];
            else if (OpProto3[0] == '\0')
                OpProto3 = OpTblPtr->Operand[0];
        }
        if (OpTblPtr->Operand[1][0]) {
            if (OpProto1[0] == '\0')
                OpProto1 = OpTblPtr->Operand[1];
            else if (OpProto2[0] == '\0')
                OpProto2 = OpTblPtr->Operand[1];
            else if (OpProto3[0] == '\0')
                OpProto3 = OpTblPtr->Operand[1];
        }
        if (OpTblPtr->Operand[2][0]) {
            if (OpProto1[0] == '\0')
                OpProto1 = OpTblPtr->Operand[2];
            else if (OpProto2[0] == '\0')
                OpProto2 = OpTblPtr->Operand[2];
            else if (OpProto3[0] == '\0')
                OpProto3 = OpTblPtr->Operand[2];
        }
    }

    //-------------------------------------
    // Check if this is a valid opcode

    if (OpTblPtr->Name[0] == '\0') {
        // if (Op2b == 0) StringPrintFormat(InstString, "DB %.2X", Opcode.Opcode); else StringPrintFormat(InstString,
        // "DB 0Fh");
        Instruction->Length = sizeof(U8);
        return Instruction->Length;
    }

    //-------------------------------------
    // Set the opcode name

    if (OpTblPtr->Name[0] != '\0') {
        StringCopy(Instruction->Name, OpTblPtr->Name);
    } else {
        Instruction->Name[0] = '\0';
    }

    //-------------------------------------
    // Get the type from any of the operands

    Instruction->Operand[0].Any.Size = Intel_GetOperandSize(Instruction, OpProto1);
    Instruction->Operand[1].Any.Size = Intel_GetOperandSize(Instruction, OpProto2);
    Instruction->Operand[2].Any.Size = Intel_GetOperandSize(Instruction, OpProto3);

    //-------------------------------------
    // Check if we need a ModR/M byte if we don't already have it

    if (HaveModR_M == 0) {
        if (Intel_IsModR_M(OpProto1[0]) || Intel_IsModR_M(OpProto2[0]) || Intel_IsModR_M(OpProto3[0])) {
            InstPtr += Intel_GetModR_M(Instruction, InstPtr);
            HaveModR_M = 1;
        }
    }

    //-------------------------------------
    // Process operand 1 if any

    if (OpProto1[0]) {
        InstPtr += Intel_Decode_Operand(Instruction, &(Instruction->Operand[0]), OpProto1, InstPtr);
        Instruction->NumOperands++;
    }

    //-------------------------------------
    // Process operand 2 if any

    if (OpProto2[0]) {
        InstPtr += Intel_Decode_Operand(Instruction, &(Instruction->Operand[1]), OpProto2, InstPtr);
        Instruction->NumOperands++;
    }

    //-------------------------------------
    // Process operand 3 if any

    if (OpProto3[0]) {
        InstPtr += Intel_Decode_Operand(Instruction, &(Instruction->Operand[2]), OpProto3, InstPtr);
        Instruction->NumOperands++;
    }

    Instruction->Length = InstPtr - (U8*)InstBuffer;

    return Instruction->Length;
}

/*************************************************************************************************/

/**
 * @brief Print type specifier string (e.g., "BYTE PTR", "DWORD PTR") for operand size.
 * @param Size Size in bits (I8BIT, I16BIT, I32BIT, I64BIT).
 * @param Buffer Output buffer to write the type specifier.
 * @return Length of written string.
 */
int Intel_PrintTypeSpec(U32 Size, LPSTR Buffer) {
    switch (Size) {
        case I8BIT:
            StringCopy(Buffer, BYTEPTR);
            break;
        case I16BIT:
            StringCopy(Buffer, WORDPTR);
            break;
        case I32BIT:
            StringCopy(Buffer, DWORDPTR);
            break;
        case I64BIT:
            StringCopy(Buffer, QWORDPTR);
            break;
    }

    return 1;
}

/*************************************************************************************************/

/**
 * @brief Convert instruction structure to assembly string representation.
 * @param Instruction Pointer to instruction structure to convert.
 * @param InstString Output buffer for assembly string.
 * @return Length of generated string.
 */
int Intel_StructureToString(LPINTEL_INSTRUCTION Instruction, LPSTR InstString) {
    STR TempBuffer[64];
    U32 c = 0;

    if (Instruction->Name[0] == '\0') {
        StringPrintFormat(InstString, TEXT("%s"), INVALID);
        return 1;
    }

    // Put the mnemonic of the instruction
    StringPrintFormat(InstString, TEXT("%s "), Instruction->Name);

    // Process the operands
    for (c = 0; c < Instruction->NumOperands; c++) {
        // Put a comma
        if (c > 0) StringConcat(InstString, TEXT(", "));

        // Put the operand
        switch (Instruction->Operand[c].Any.Type) {
            case INTEL_OPERAND_TYPE_R: {
                StringConcat(InstString, Intel_RegNames[Instruction->Operand[c].R.Register]);
            } break;

            case INTEL_OPERAND_TYPE_I8: {
                StringPrintFormat(TempBuffer, TEXT("%x"), (U32)Instruction->Operand[c].I8.Value);
                StringConcat(InstString, TempBuffer);
            } break;

            case INTEL_OPERAND_TYPE_I16: {
                StringPrintFormat(TempBuffer, TEXT("%x"), (U32)Instruction->Operand[c].I16.Value);
                StringConcat(InstString, TempBuffer);
            } break;

            case INTEL_OPERAND_TYPE_I32: {
                StringPrintFormat(TempBuffer, TEXT("%x"), (U32)Instruction->Operand[c].I32.Value);
                StringConcat(InstString, TempBuffer);
            } break;

            case INTEL_OPERAND_TYPE_I64: {
                // StringPrintFormat(TempBuffer, "%x", (U64) Instruction->Operand[c].I64.Value);
                // StringConcat(InstString, TempBuffer);
            } break;

            case INTEL_OPERAND_TYPE_DSP: {
                U32 Address = (U32)Instruction->Address + Instruction->Length + (I32)Instruction->Operand[c].DSP.Value;
                StringPrintFormat(TempBuffer, TEXT("%x"), Address);
                StringConcat(InstString, TempBuffer);
            } break;

            case INTEL_OPERAND_TYPE_II: {
                Intel_PrintTypeSpec(Instruction->Operand[c].II.Size, TempBuffer);
                StringConcat(InstString, TempBuffer);
                StringConcat(InstString, TEXT(" "));
                StringPrintFormat(TempBuffer, TEXT("[%x]"), (U32)Instruction->Operand[c].II.Value);
                StringConcat(InstString, TempBuffer);
            } break;

            case INTEL_OPERAND_TYPE_BISD: {
                LPINTEL_OPERAND_BISD BISD = &(Instruction->Operand[c].BISD);
                Intel_PrintTypeSpec(BISD->Size, TempBuffer);

                StringConcat(InstString, TempBuffer);
                StringConcat(InstString, TEXT(" ["));

                if (BISD->Base != 0x00) {
                    StringConcat(InstString, Intel_RegNames[BISD->Base]);
                }

                if (BISD->Displace != 0x00) {
                    if (BISD->Displace > 0) {
                        StringPrintFormat(TempBuffer, TEXT("+%x"), (U32)BISD->Displace);
                    } else {
                        StringPrintFormat(TempBuffer, TEXT("-%x"), (U32)-BISD->Displace);
                    }
                    StringConcat(InstString, TempBuffer);
                }

                if (BISD->Index != 0x00) {
                    if (BISD->Base != 0x00 || BISD->Displace != 0x00) {
                        StringConcat(InstString, TEXT("+"));
                    }
                    StringConcat(InstString, Intel_RegNames[BISD->Index]);

                    if (BISD->Scale != 0x00 && BISD->Scale != 0x01) {
                        StringPrintFormat(TempBuffer, TEXT("*%u"), BISD->Scale);
                        StringConcat(InstString, TempBuffer);
                    }
                }

                StringConcat(InstString, TEXT("]"));
            } break;

            case INTEL_OPERAND_TYPE_SO16: {
                StringPrintFormat(
                    TempBuffer, TEXT("%x:%x"), (U32)Instruction->Operand[c].SO16.Segment,
                    (U32)Instruction->Operand[c].SO16.Offset);
                StringConcat(InstString, TempBuffer);
            } break;

            case INTEL_OPERAND_TYPE_SO32: {
                StringPrintFormat(
                    TempBuffer, TEXT("%x:%x"), (U32)Instruction->Operand[c].SO32.Segment,
                    (U32)Instruction->Operand[c].SO32.Offset);
                StringConcat(InstString, TempBuffer);
            } break;

            case INTEL_OPERAND_TYPE_STR: {
                StringConcat(InstString, Instruction->Operand[c].STR.String);
            } break;
        }
    }

    return 1;
}

/*************************************************************************************************/

/**
 * @brief Decode machine code bytes directly to assembly string.
 * @param Base Base address for relative addressing calculations.
 * @param InstBuffer Pointer to instruction bytes to decode.
 * @param InstString Buffer to store the resulting assembly string.
 * @return Length of decoded instruction in bytes.
 */
U32 Intel_MachineCodeToString(LPCSTR Base, LPCSTR InstBuffer, LPSTR InstString) {
    INTEL_INSTRUCTION Instruction;
    long Length = Intel_MachineCodeToStructure(Base, InstBuffer, &Instruction);
    Intel_StructureToString(&Instruction, InstString);
    return Length;
}

/*************************************************************************************************/

#define MCSET(Name, Type, Data)                                     \
    {                                                               \
        MemoryCopy(Name->Code + Name->Size, &(Data), sizeof(Type)); \
        Name->Size += sizeof(Type);                                 \
    };

/*************************************************************************************************/

/**
 * @brief Convert instruction structure to machine code bytes.
 * @param Instruction Pointer to instruction structure to convert.
 * @param MachineCode Pointer to machine code structure to fill.
 * @return Number of bytes generated.
 */
U32 Intel_StructureToMachineCode(LPINTEL_INSTRUCTION Instruction, LPINTEL_MACHINE_CODE MachineCode) {
    U32 MemoryAddressing = 0;
    U32 FoundPrototype = 0;
    U32 c = 0;
    U32 d = 0;
    U32 Register = 0;
    U8* MachineCodePtr = MachineCode->Code;
    INTEL_MODR_M ModR_M;
    INTEL_SIB SIB;

    //-------------------------------------

    // Shortcut to intel instruction

    STR Name[16];
    U32 Immediate = 0;

    U32 Have_ModR_M = 0;
    U32 Have_SIB = 0;
    U32 Have_Immediate = 0;

    //-------------------------------------

    // Variables used to search prototype

    INTEL_OPCODE_PROTOTYPE* OpTblPtr = NULL;
    INTEL_OPCODE_PROTOTYPE Prototype;
    STR ProtoOperand[INTEL_MAX_OPERANDS][8];

    //-------------------------------------

    // Setup instruction

    StringCopy(Name, Instruction->Name);

    Instruction->ModR_M.Byte = 0;
    Instruction->SIB.Byte = 0;

    //-------------------------------------

    // Setup machine code

    MemorySet(MachineCode, 0, sizeof(INTEL_MACHINE_CODE));

    //-------------------------------------

    // Setup prototype

    Prototype.Name = Name;

    for (c = 0; c < INTEL_MAX_OPERANDS; c++) {
        ProtoOperand[c][0] = '\0';
        Prototype.Operand[c] = ProtoOperand[c];
    }

    //---------------------------------------------

    // Check validity of instruction

    // Reject instructions using memory addressing in both operands

    for (c = 0; c < Instruction->NumOperands; c++) {
        if (Instruction->Operand[c].Any.Type == INTEL_OPERAND_TYPE_DSP ||
            Instruction->Operand[c].Any.Type == INTEL_OPERAND_TYPE_II ||
            Instruction->Operand[c].Any.Type == INTEL_OPERAND_TYPE_BI ||
            Instruction->Operand[c].Any.Type == INTEL_OPERAND_TYPE_BISD ||
            Instruction->Operand[c].Any.Type == INTEL_OPERAND_TYPE_SO16 ||
            Instruction->Operand[c].Any.Type == INTEL_OPERAND_TYPE_SO32) {
            if (MemoryAddressing == 1) {
                return 0;
            } else {
                MemoryAddressing = 1;
            }
        }
    }

    //---------------------------------------------

    // Handle special cases

    if (Instruction->NumOperands == 0) {
        // NOP
        if (StringCompare(Name, TEXT("NOP")) == 0) {
            MachineCode->Code[0] = 0x90;
            MachineCode->Size = sizeof(U8);
            return MachineCode->Size;
        }
    }

    if (Instruction->NumOperands == 1) {
        // Interrupt call
        if (StringCompare(Name, TEXT("INT")) == 0) {
            if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_I32) {
                U8 Opcode = 0;
                U8 Interrupt = 0;

                Interrupt = (U8)Instruction->Operand[0].I32.Value;

                if (Interrupt == 0x03) {
                    Opcode = 0xCC;
                    MCSET(MachineCode, U8, Opcode);
                } else {
                    Opcode = 0xCD;
                    MCSET(MachineCode, U8, Opcode);
                    MCSET(MachineCode, U8, Interrupt);
                }

                return MachineCode->Size;
            }
        }

        // PUSH 16 bit register
        if (StringCompare(Name, TEXT("PUSH")) == 0) {
            if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_R) {
                Register = Instruction->Operand[0].R.Register;

                // If 32 bit general purpose register, set to 16 bit
                if (Register >= INTEL_REG_EAX && Register <= INTEL_REG_EDI) {
                    Register = INTEL_REG_AX + (Register - INTEL_REG_EAX);
                }

                if ((Register >= INTEL_REG_AX && Register <= INTEL_REG_DI) ||
                    (Register >= INTEL_REG_ES && Register <= INTEL_REG_GS)) {
                    switch (Instruction->Operand[0].R.Register) {
                        case INTEL_REG_AX:
                            MachineCode->Code[0] = 0x50;
                            break;
                        case INTEL_REG_CX:
                            MachineCode->Code[0] = 0x51;
                            break;
                        case INTEL_REG_DX:
                            MachineCode->Code[0] = 0x52;
                            break;
                        case INTEL_REG_BX:
                            MachineCode->Code[0] = 0x53;
                            break;
                        case INTEL_REG_SP:
                            MachineCode->Code[0] = 0x54;
                            break;
                        case INTEL_REG_BP:
                            MachineCode->Code[0] = 0x55;
                            break;
                        case INTEL_REG_SI:
                            MachineCode->Code[0] = 0x56;
                            break;
                        case INTEL_REG_DI:
                            MachineCode->Code[0] = 0x57;
                            break;
                        case INTEL_REG_ES:
                            MachineCode->Code[0] = 0x06;
                            break;
                        case INTEL_REG_CS:
                            MachineCode->Code[0] = 0x0E;
                            break;
                        case INTEL_REG_SS:
                            MachineCode->Code[0] = 0x16;
                            break;
                        case INTEL_REG_DS:
                            MachineCode->Code[0] = 0x1E;
                            break;
                        case INTEL_REG_FS:
                            MachineCode->Code[0] = 0xA0;
                            break;
                        case INTEL_REG_GS:
                            MachineCode->Code[0] = 0xA8;
                            break;
                    }
                    MachineCode->Size = 1;
                    return MachineCode->Size;
                }
            }
        }

        // POP 16 bit register
        if (StringCompare(Name, TEXT("POP")) == 0) {
            if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_R) {
                Register = Instruction->Operand[0].R.Register;

                if ((Register >= INTEL_REG_AX && Register <= INTEL_REG_DI) ||
                    (Register >= INTEL_REG_ES && Register <= INTEL_REG_GS)) {
                    switch (Instruction->Operand[0].R.Register) {
                        case INTEL_REG_AX:
                            MachineCode->Code[0] = 0x58;
                            break;
                        case INTEL_REG_CX:
                            MachineCode->Code[0] = 0x59;
                            break;
                        case INTEL_REG_DX:
                            MachineCode->Code[0] = 0x5A;
                            break;
                        case INTEL_REG_BX:
                            MachineCode->Code[0] = 0x5B;
                            break;
                        case INTEL_REG_SP:
                            MachineCode->Code[0] = 0x5C;
                            break;
                        case INTEL_REG_BP:
                            MachineCode->Code[0] = 0x5D;
                            break;
                        case INTEL_REG_SI:
                            MachineCode->Code[0] = 0x5E;
                            break;
                        case INTEL_REG_DI:
                            MachineCode->Code[0] = 0x5F;
                            break;
                        case INTEL_REG_ES:
                            MachineCode->Code[0] = 0x07;
                            break;
                        case INTEL_REG_CS:
                            MachineCode->Code[0] = 0x0E;
                            break;
                        case INTEL_REG_SS:
                            MachineCode->Code[0] = 0x17;
                            break;
                        case INTEL_REG_DS:
                            MachineCode->Code[0] = 0x1F;
                            break;
                        case INTEL_REG_FS:
                            MachineCode->Code[0] = 0xA1;
                            break;
                        case INTEL_REG_GS:
                            MachineCode->Code[0] = 0xA9;
                            break;
                    }
                    MachineCode->Size = 1;
                    return MachineCode->Size;
                }
            }
        }

        // INC 16 bit register
        if (StringCompare(Name, TEXT("INC")) == 0) {
            if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_R) {
                Register = Instruction->Operand[0].R.Register;

                if (Register >= INTEL_REG_AX && Register <= INTEL_REG_DI) {
                    switch (Instruction->Operand[0].R.Register) {
                        case INTEL_REG_AX:
                            MachineCode->Code[0] = 0x40;
                            break;
                        case INTEL_REG_CX:
                            MachineCode->Code[0] = 0x41;
                            break;
                        case INTEL_REG_DX:
                            MachineCode->Code[0] = 0x42;
                            break;
                        case INTEL_REG_BX:
                            MachineCode->Code[0] = 0x43;
                            break;
                        case INTEL_REG_SP:
                            MachineCode->Code[0] = 0x44;
                            break;
                        case INTEL_REG_BP:
                            MachineCode->Code[0] = 0x45;
                            break;
                        case INTEL_REG_SI:
                            MachineCode->Code[0] = 0x46;
                            break;
                        case INTEL_REG_DI:
                            MachineCode->Code[0] = 0x47;
                            break;
                    }
                    MachineCode->Size = 1;
                    return MachineCode->Size;
                }
            }
        }

        // DEC 16 bit register
        if (StringCompare(Name, TEXT("DEC")) == 0) {
            if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_R) {
                Register = Instruction->Operand[0].R.Register;

                if (Register >= INTEL_REG_AX && Register <= INTEL_REG_DI) {
                    switch (Instruction->Operand[0].R.Register) {
                        case INTEL_REG_AX:
                            MachineCode->Code[0] = 0x48;
                            break;
                        case INTEL_REG_CX:
                            MachineCode->Code[0] = 0x49;
                            break;
                        case INTEL_REG_DX:
                            MachineCode->Code[0] = 0x4A;
                            break;
                        case INTEL_REG_BX:
                            MachineCode->Code[0] = 0x4B;
                            break;
                        case INTEL_REG_SP:
                            MachineCode->Code[0] = 0x4C;
                            break;
                        case INTEL_REG_BP:
                            MachineCode->Code[0] = 0x4D;
                            break;
                        case INTEL_REG_SI:
                            MachineCode->Code[0] = 0x4E;
                            break;
                        case INTEL_REG_DI:
                            MachineCode->Code[0] = 0x4F;
                            break;
                    }
                    MachineCode->Size = 1;
                    return MachineCode->Size;
                }
            }
        }
    }

    if (Instruction->NumOperands == 2) {
        if (StringCompare(Name, TEXT("XCHG")) == 0) {
            if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_R &&
                Instruction->Operand[1].Any.Type == INTEL_OPERAND_TYPE_R) {
                U32 Reg1 = Instruction->Operand[0].R.Register;
                U32 Reg2 = Instruction->Operand[1].R.Register;

                if (Reg1 == INTEL_REG_AX && Reg2 >= INTEL_REG_CX && Reg2 <= INTEL_REG_DI) {
                    MachineCode->Code[0] = 0x91 + (Reg2 - INTEL_REG_CX);
                    MachineCode->Size = 1;
                    return MachineCode->Size;
                }
            }
        }

        // MOV reg8/reg16, imm

        if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_R &&
            Instruction->Operand[1].Any.Type == INTEL_OPERAND_TYPE_I32) {
            U32 Register = Instruction->Operand[0].R.Register - INTEL_REG_AL;
            U8 Opcode = 0xB0 + Register;

            MCSET(MachineCode, U8, Opcode);

            switch (Instruction->OperandSize) {
                case I8BIT:
                    MCSET(MachineCode, U8, Instruction->Operand[1].I32.Value);
                    break;
                case I16BIT:
                    MCSET(MachineCode, U16, Instruction->Operand[1].I32.Value);
                    break;
                case I32BIT:
                    MCSET(MachineCode, U32, Instruction->Operand[1].I32.Value);
                    break;
            }

            return MachineCode->Size;
        }

        // MOV AL/AX, [imm]

        if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_R &&
            Instruction->Operand[1].Any.Type == INTEL_OPERAND_TYPE_II) {
            U32 Register = Instruction->Operand[0].R.Register;

            if (Register == INTEL_REG_AL || Register == INTEL_REG_AX || Register == INTEL_REG_EAX) {
                U8 Opcode = 0;

                if (Register == INTEL_REG_EAX) Register = INTEL_REG_AX;

                if (Register == INTEL_REG_AL) Opcode = 0xA0;
                if (Register == INTEL_REG_AX) Opcode = 0xA1;

                MCSET(MachineCode, U8, Opcode);

                switch (Instruction->OperandSize) {
                    case I8BIT:
                        MCSET(MachineCode, U8, Instruction->Operand[1].II.Value);
                        break;
                    case I16BIT:
                        MCSET(MachineCode, U16, Instruction->Operand[1].II.Value);
                        break;
                    case I32BIT:
                        MCSET(MachineCode, U32, Instruction->Operand[1].II.Value);
                        break;
                }

                return MachineCode->Size;
            }
        }

        // MOV [imm], AL/AX

        if (Instruction->Operand[0].Any.Type == INTEL_OPERAND_TYPE_II &&
            Instruction->Operand[1].Any.Type == INTEL_OPERAND_TYPE_R) {
            U32 Register = Instruction->Operand[1].R.Register;

            if (Register == INTEL_REG_AL || Register == INTEL_REG_AX || Register == INTEL_REG_EAX) {
                U8 Opcode = 0;

                if (Register == INTEL_REG_EAX) Register = INTEL_REG_AX;

                if (Register == INTEL_REG_AL) Opcode = 0xA2;
                if (Register == INTEL_REG_AX) Opcode = 0xA3;

                MCSET(MachineCode, U8, Opcode);

                switch (Instruction->OperandSize) {
                    case I8BIT:
                        MCSET(MachineCode, U8, Instruction->Operand[0].II.Value);
                        break;
                    case I16BIT:
                        MCSET(MachineCode, U16, Instruction->Operand[0].II.Value);
                        break;
                    case I32BIT:
                        MCSET(MachineCode, U32, Instruction->Operand[0].II.Value);
                        break;
                }

                return MachineCode->Size;
            }
        }
    }

    //-------------------------------------

    // Translate the instruction into an opcode prototype

    for (c = 0; c < Instruction->NumOperands; c++) {
        switch (Instruction->Operand[c].Any.Type) {
            case INTEL_OPERAND_TYPE_R: {
                // Select either G or E

                for (d = 0; d < Instruction->NumOperands; d++) {
                    if (d != c) {
                        if (ProtoOperand[d][0] == 'E') {
                            StringConcat(ProtoOperand[c], TEXT("G"));
                            break;
                        }
                        if (ProtoOperand[d][0] == 'G') {
                            StringConcat(ProtoOperand[c], TEXT("E"));
                            break;
                        }
                    }
                }

                if (ProtoOperand[c][0] == '\0') {
                    StringConcat(ProtoOperand[c], TEXT("G"));
                }

                // Add the type
                switch (Instruction->OperandSize) {
                    case I8BIT:
                        StringConcat(ProtoOperand[c], TEXT("b"));
                        break;
                    case I16BIT:
                        StringConcat(ProtoOperand[c], TEXT("v"));
                        break;
                    case I32BIT:
                        StringConcat(ProtoOperand[c], TEXT("v"));
                        break;
                }
            } break;

            case INTEL_OPERAND_TYPE_I32: {
                // If the instruction is a conditional jump, need to change
                // the immediate value to a relative displacement

                if (StringCompare(Name, TEXT("JO")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JNO")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JB")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JNB")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JZ")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JNZ")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JBE")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JNBE")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JS")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JNS")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JP")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JNP")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JL")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JNL")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JLE")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JNLE")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("JMP")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Jv"));
                else if (StringCompare(Name, TEXT("CALL")) == 0)
                    StringConcat(ProtoOperand[c], TEXT("Ap"));
                else {
                    StringConcat(ProtoOperand[c], TEXT("I"));

                    // Add the type
                    switch (Instruction->OperandSize) {
                        case I8BIT:
                            StringConcat(ProtoOperand[c], TEXT("b"));
                            break;
                        case I16BIT:
                            StringConcat(ProtoOperand[c], TEXT("v"));
                            break;
                        case I32BIT:
                            StringConcat(ProtoOperand[c], TEXT("v"));
                            break;
                    }
                }
            } break;

            case INTEL_OPERAND_TYPE_II: {
                StringConcat(ProtoOperand[c], TEXT("O"));

                // Add the type
                switch (Instruction->OperandSize) {
                    case I8BIT:
                        StringConcat(ProtoOperand[c], TEXT("b"));
                        break;
                    case I16BIT:
                        StringConcat(ProtoOperand[c], TEXT("v"));
                        break;
                    case I32BIT:
                        StringConcat(ProtoOperand[c], TEXT("v"));
                        break;
                }
            } break;

            case INTEL_OPERAND_TYPE_BI: {
                StringConcat(ProtoOperand[c], TEXT("E"));

                // Add the type
                switch (Instruction->OperandSize) {
                    case I8BIT:
                        StringConcat(ProtoOperand[c], TEXT("b"));
                        break;
                    case I16BIT:
                        StringConcat(ProtoOperand[c], TEXT("v"));
                        break;
                    case I32BIT:
                        StringConcat(ProtoOperand[c], TEXT("v"));
                        break;
                }
            } break;

            case INTEL_OPERAND_TYPE_BISD: {
                // First see if we can use a 16 bit BI instead of the SIB byte
                // Note that the SIB byte is used only with 32 bit instructions

                // Is it a [reg+reg] operand ?

                if (Instruction->Operand[c].BISD.Scale == 1 && Instruction->Operand[c].BISD.Displace == 0) {
                    StringConcat(ProtoOperand[c], TEXT("E"));
                } else
                    // Is it a [reg+imm] / +imm[reg] operand ?
                    if (Instruction->Operand[c].BISD.Scale == 1 && Instruction->Operand[c].BISD.Index == 0) {
                        StringConcat(ProtoOperand[c], TEXT("E"));
                    }

                // Add the type
                switch (Instruction->OperandSize) {
                    case I8BIT:
                        StringConcat(ProtoOperand[c], TEXT("b"));
                        break;
                    case I16BIT:
                        StringConcat(ProtoOperand[c], TEXT("v"));
                        break;
                    case I32BIT:
                        StringConcat(ProtoOperand[c], TEXT("v"));
                        break;
                }
            } break;
        }
    }

    // Search in the opcode table for a match

    for (c = 0; c < 512; c++) {
        // Point to the current prototype
        OpTblPtr = Opcode_Table + c;

        if ((STRINGS_EQUAL(OpTblPtr->Name, Prototype.Name)) &&
            (STRINGS_EQUAL(OpTblPtr->Operand[0], Prototype.Operand[0])) &&
            (STRINGS_EQUAL(OpTblPtr->Operand[1], Prototype.Operand[1])) &&
            (STRINGS_EQUAL(OpTblPtr->Operand[2], Prototype.Operand[2]))) {
            FoundPrototype = 1;

            if (c < 256) {
                // One byte opcode
                *MachineCodePtr = c;
                MachineCodePtr += sizeof(U8);
                MachineCode->Size += sizeof(U8);
            } else {
                // Two byte opcode
                *MachineCodePtr = 0x0F;
                MachineCodePtr += sizeof(U8);
                MachineCode->Size += sizeof(U8);

                *MachineCodePtr = c - 0x0100;
                MachineCodePtr += sizeof(U8);
                MachineCode->Size += sizeof(U8);
            }

            break;
        }
    }

    if (FoundPrototype == 0) {
        /*
        StringPrintFormat(Temp, "Invalid instruction (%s %s %s %s)", Prototype.Name,
                Prototype.Operand[0], Prototype.Operand[1], Prototype.Operand[2]);
        FatalError(Temp);
        */

        return 0;
    }

    //-------------------------------------

    // Encode the instruction given the prototype

    for (c = 0; c < Instruction->NumOperands; c++) {
        switch (Instruction->Operand[c].Any.Type) {
            case INTEL_OPERAND_TYPE_R: {
                Register = Instruction->Operand[c].R.Register;

                if (Register >= INTEL_REG_AL && Register <= INTEL_REG_BH) {
                    Register -= INTEL_REG_8;
                } else if (Register >= INTEL_REG_AX && Register <= INTEL_REG_DI) {
                    Register -= INTEL_REG_16;
                } else if (Register >= INTEL_REG_EAX && Register <= INTEL_REG_EDI) {
                    Register -= INTEL_REG_32;
                } else if (Register >= INTEL_REG_MM0 && Register <= INTEL_REG_MM7) {
                    Register -= INTEL_REG_64;
                } else if (Register >= INTEL_REG_ES && Register <= INTEL_REG_GS) {
                    Register -= INTEL_REG_SEG;
                } else if (Register >= INTEL_REG_CR0 && Register <= INTEL_REG_CR4) {
                    Register -= INTEL_REG_CRT;
                }

                // Encode the register value in either the Reg or R_M field
                if (Prototype.Operand[c][0] == 'G') {
                    Have_ModR_M = 1;
                    ModR_M.Bits.Reg = Register;
                } else if (Prototype.Operand[c][0] == 'E') {
                    Have_ModR_M = 1;
                    ModR_M.Bits.Mod = 0x03;
                    ModR_M.Bits.R_M = Register;
                }
            } break;

            case INTEL_OPERAND_TYPE_I32: {
                Have_Immediate = 1;
                Immediate = Instruction->Operand[c].I32.Value;
            } break;

            case INTEL_OPERAND_TYPE_II: {
                Have_Immediate = 1;
                Immediate = Instruction->Operand[c].I32.Value;
            } break;

            case INTEL_OPERAND_TYPE_BI: {
                U32 Base = Instruction->Operand[c].BI.Base;
                U32 Index = Instruction->Operand[c].BI.Index;

                Have_ModR_M = 1;

                // Encode the base and index in the ModR/M byte
                if (Base == INTEL_REG_SI && Index == 0)
                    ModR_M.Bits.R_M = 0x04;
                else if (Base == INTEL_REG_DI && Index == 0)
                    ModR_M.Bits.R_M = 0x05;
                else if (Base == INTEL_REG_BX && Index == 0)
                    ModR_M.Bits.R_M = 0x07;
                else if (Base == INTEL_REG_BX && Index == INTEL_REG_SI)
                    ModR_M.Bits.R_M = 0x00;
                else if (Base == INTEL_REG_BX && Index == INTEL_REG_DI)
                    ModR_M.Bits.R_M = 0x01;
                else if (Base == INTEL_REG_BP && Index == INTEL_REG_SI)
                    ModR_M.Bits.R_M = 0x02;
                else if (Base == INTEL_REG_BP && Index == INTEL_REG_DI)
                    ModR_M.Bits.R_M = 0x03;
                else {
                    return 0;
                }
            } break;
        }
    }

    //-------------------------------------

    // Record the ModR/M byte if used
    if (Have_ModR_M) {
        MachineCode->Offset_ModR_M = MachineCode->Size;
        MCSET(MachineCode, INTEL_MODR_M, ModR_M);
    }

    // Record the SIB byte if used
    if (Have_SIB) {
        MachineCode->Offset_SIB = MachineCode->Size;
        MCSET(MachineCode, INTEL_SIB, SIB);
    }

    // Record the immediate value if used
    if (Have_Immediate) {
        MachineCode->Offset_Imm = MachineCode->Size;

        switch (Instruction->OperandSize) {
            case I8BIT:
                MCSET(MachineCode, U8, Immediate);
                break;
            case I16BIT:
                MCSET(MachineCode, U16, Immediate);
                break;
            case I32BIT:
                MCSET(MachineCode, U32, Immediate);
                break;
        }
    }

    //-------------------------------------

    return MachineCode->Size;
}

/*************************************************************************************************/

/**
 * @brief Set default operand and address sizes for instruction decoding.
 * @param OperandSize Default operand size (I16BIT or I32BIT).
 * @param AddressSize Default address size (I16BIT or I32BIT).
 * @return Always returns 1.
 */
int SetIntelAttributes(long OperandSize, long AddressSize) {
    IntelOperandSize = OperandSize;
    IntelAddressSize = AddressSize;

    return 1;
}

