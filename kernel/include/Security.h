
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


    Security definitions

\************************************************************************/

#ifndef SECURITY_H_INCLUDED
#define SECURITY_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "List.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define EXOS_PRIVILEGE_KERNEL 0x0
#define EXOS_PRIVILEGE_ADMIN 0x1
#define EXOS_PRIVILEGE_USER 0x2

/************************************************************************/

#define MAX_SPECIFIC_PERMISSIONS 16

typedef struct tag_SECURITY {
    LISTNODE_FIELDS
    U64 Owner;  // Owner ID (hash)
    U32 UserPermissionCount;
    U32 DefaultPermissions;
    struct {
        U64 UserHash;  // User ID
        U32 Permissions;
    } UserPerms[MAX_SPECIFIC_PERMISSIONS];
} SECURITY, *LPSECURITY;

#define PERMISSION_NONE 0x00000000
#define PERMISSION_EXECUTE 0x00000001
#define PERMISSION_READ 0x00000002
#define PERMISSION_WRITE 0x00000004

// Macro to initialize a security

#define EMPTY_SECURITY \
    { .TypeID = KOID_SECURITY, .References = 1, .Next = NULL, .Prev = NULL, .Owner = 0, .UserPermissionCount = 0, .DefaultPermissions = PERMISSION_NONE }

/************************************************************************/

#pragma pack(pop)

#endif  // SECURITY_H_INCLUDED
