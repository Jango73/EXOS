
// Segment.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#include "../include/Base.h"
#include "../include/I386.h"
#include "../include/Kernel.h"
#include "../include/String.h"

/***************************************************************************/

void InitSegmentDescriptor(LPSEGMENTDESCRIPTOR This, U32 Type) {
    MemorySet(This, 0, sizeof(SEGMENTDESCRIPTOR));

    This->Limit_00_15 = 0xFFFF;
    This->Base_00_15 = 0x0000;
    This->Base_16_23 = 0x00;
    This->Accessed = 0;
    This->CanWrite = 1;
    This->ConformExpand = 1;
    This->Type = Type;
    This->Segment = 1;
    This->Privilege = PRIVILEGE_USER;
    This->Present = 1;
    This->Limit_16_19 = 0x0F;
    This->Available = 0;
    This->OperandSize = 1;
    This->Granularity = GDT_GRANULAR_4KB;
    This->Base_24_31 = 0x00;
}

/***************************************************************************/

void InitGlobalDescriptorTable(LPSEGMENTDESCRIPTOR Table) {
    MemorySet(Table, 0, GDT_SIZE);

    InitSegmentDescriptor(&Table[2], GDT_TYPE_CODE);
    Table[2].Privilege = GDT_PRIVILEGE_KERNEL;

    InitSegmentDescriptor(&Table[3], GDT_TYPE_DATA);
    Table[3].Privilege = GDT_PRIVILEGE_KERNEL;

    InitSegmentDescriptor(&Table[4], GDT_TYPE_CODE);
    Table[4].Privilege = GDT_PRIVILEGE_USER;

    InitSegmentDescriptor(&Table[5], GDT_TYPE_DATA);
    Table[5].Privilege = GDT_PRIVILEGE_USER;

    InitSegmentDescriptor(&Table[6], GDT_TYPE_CODE);
    Table[6].Privilege = GDT_PRIVILEGE_KERNEL;
    Table[6].OperandSize = GDT_OPERANDSIZE_16;
    Table[6].Granularity = GDT_GRANULAR_1B;
    SetSegmentDescriptorLimit(&Table[6], N_1MB_M1);

    InitSegmentDescriptor(&Table[7], GDT_TYPE_DATA);
    Table[7].Privilege = GDT_PRIVILEGE_KERNEL;
    Table[7].OperandSize = GDT_OPERANDSIZE_16;
    Table[7].Granularity = GDT_GRANULAR_1B;
    SetSegmentDescriptorLimit(&Table[7], N_1MB_M1);
}

/***************************************************************************/

void SetSegmentDescriptorBase(LPSEGMENTDESCRIPTOR This, U32 Base) {
    This->Base_00_15 = (Base & (U32)0x0000FFFF) >> 0x00;
    This->Base_16_23 = (Base & (U32)0x00FF0000) >> 0x10;
    This->Base_24_31 = (Base & (U32)0xFF000000) >> 0x18;
}

/***************************************************************************/

void SetSegmentDescriptorLimit(LPSEGMENTDESCRIPTOR This, U32 Limit) {
    This->Limit_00_15 = (Limit >> 0x00) & 0x0000FFFF;
    This->Limit_16_19 = (Limit >> 0x10) & 0x0000000F;
}

/***************************************************************************/

void SetTSSDescriptorBase(LPTSSDESCRIPTOR This, U32 Base) { SetSegmentDescriptorBase((LPSEGMENTDESCRIPTOR)This, Base); }

/***************************************************************************/

void SetTSSDescriptorLimit(LPTSSDESCRIPTOR This, U32 Limit) {
    SetSegmentDescriptorLimit((LPSEGMENTDESCRIPTOR)This, Limit);
}

/***************************************************************************/
