
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
