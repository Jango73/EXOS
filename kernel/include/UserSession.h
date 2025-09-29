
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


    Session Management

\************************************************************************/

#ifndef USERSESSION_H_INCLUDED
#define USERSESSION_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "List.h"
#include "UserAccount.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define SESSION_TIMEOUT_MINUTES 30
#define SESSION_TIMEOUT_MS (SESSION_TIMEOUT_MINUTES * 60 * 1000)

/************************************************************************/

// Functions in Session.c
BOOL InitializeSessionSystem(void);
void ShutdownSessionSystem(void);
LPUSERSESSION CreateUserSession(U64 UserID, HANDLE ShellTask);
BOOL ValidateUserSession(LPUSERSESSION Session);
void DestroyUserSession(LPUSERSESSION Session);
void TimeoutInactiveSessions(void);
LPUSERSESSION FindSessionByTask(HANDLE Task);
LPUSERSESSION GetCurrentSession(void);
void UpdateSessionActivity(LPUSERSESSION Session);

/************************************************************************/

#pragma pack(pop)

#endif  // USERSESSION_H_INCLUDED
