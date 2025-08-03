
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef VARARGS_H_INCLUDED
#define VARARGS_H_INCLUDED

/***************************************************************************/

typedef LPSTR VarArgList[1];

#define VarArgStart(ap, pn)                                                  \
    ((ap)[0] =                                                               \
         (LPSTR)&pn + ((sizeof(pn) + sizeof(int) - 1) & ~(sizeof(int) - 1)), \
     (void)0)

#define VarArg(ap, type)                                                 \
    ((ap)[0] += ((sizeof(type) + sizeof(int) - 1) & ~(sizeof(int) - 1)), \
     (*(type*)((ap)[0] -                                                 \
               ((sizeof(type) + sizeof(int) - 1) & ~(sizeof(int) - 1)))))

#define VarArgEnd(ap) ((ap)[0] = 0, (void)0)

/***************************************************************************/

#endif
