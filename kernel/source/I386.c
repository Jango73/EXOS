
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/I386.h"

#include "../include/Log.h"
#include "../include/String.h"
#include "../include/Text.h"

/************************************************************************/

BOOL GetSegmentInfo(LPSEGMENTDESCRIPTOR This, LPSEGMENTINFO Info) {
    if (Info) {
        Info->Base = SEGMENTBASE(This);
        Info->Limit = SEGMENTLIMIT(This);
        Info->Type = This->Type;
        Info->Privilege = This->Privilege;
        Info->Granularity = SEGMENTGRANULAR(This);
        Info->CanWrite = This->CanWrite;
        Info->OperandSize = This->OperandSize ? 32 : 16;
        Info->Conforming = This->ConformExpand;
        Info->Present = This->Present;

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL SegmentInfoToString(LPSEGMENTINFO This, LPSTR Text) {
    if (This && Text) {
        STR Temp[64];

        Text[0] = STR_NULL;

        StringConcat(Text, TEXT("Segment"));
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Base           : "));
        U32ToHexString(This->Base, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Limit          : "));
        U32ToHexString(This->Limit, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Type           : "));
        StringConcat(Text, This->Type ? TEXT("Code") : TEXT("Data"));
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Privilege      : "));
        U32ToHexString(This->Privilege, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Granularity    : "));
        U32ToHexString(This->Granularity, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Can write      : "));
        StringConcat(Text, This->CanWrite ? TEXT("True") : TEXT("False"));
        StringConcat(Text, Text_NewLine);

        /*
            StringConcat(Text, "Operand Size   : ");
            StringConcat(Text, This->OperandSize ? "32-bit" : "16-bit");
            StringConcat(Text, Text_NewLine);

            StringConcat(Text, "Conforming     : ");
            StringConcat(Text, This->Conforming ? "True" : "False");
            StringConcat(Text, Text_NewLine);

            StringConcat(Text, "Present        : ");
            StringConcat(Text, This->Present ? "True" : "False");
            StringConcat(Text, Text_NewLine);
        */

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL DumpGlobalDescriptorTable(LPSEGMENTDESCRIPTOR Table, U32 Size) {
    U32 Index = 0;

    if (Table) {
        SEGMENTINFO Info;
        STR Text[256];

        for (Index = 0; Index < Size; Index++) {
            GetSegmentInfo(Table + Index, &Info);
            SegmentInfoToString(&Info, Text);
            KernelLogText(LOG_DEBUG, Text);
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

void DumpRegisters(LPINTEL386REGISTERS Regs) {
    KernelLogText(
        LOG_VERBOSE, TEXT("EAX : %X EBX : %X ECX : %X EDX : %X "), Regs->EAX, Regs->EBX, Regs->ECX, Regs->EDX);
    KernelLogText(LOG_VERBOSE, TEXT("ESI : %X EDI : %X EBP : %X "), Regs->ESI, Regs->EDI, Regs->EBP);
    KernelLogText(LOG_VERBOSE, TEXT("CS : %X DS : %X SS : %X "), Regs->CS, Regs->DS, Regs->SS);
    KernelLogText(LOG_VERBOSE, TEXT("ES : %X FS : %X GS : %X "), Regs->ES, Regs->FS, Regs->GS);
    KernelLogText(LOG_VERBOSE, TEXT("E-flags : %X EIP : %X "), Regs->EFlags, Regs->EIP);
    KernelLogText(
        LOG_VERBOSE, TEXT("CR0 : %X CR2 : %X CR3 : %X CR4 : %X "), Regs->CR0, Regs->CR2, Regs->CR3, Regs->CR4);
    KernelLogText(
        LOG_VERBOSE, TEXT("DR0 : %X DR1 : %X DR2 : %X DR3 : %X "), Regs->DR0, Regs->DR1, Regs->DR2, Regs->DR3);
}
