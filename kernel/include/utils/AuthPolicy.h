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


    Authentication attempt policy helper

\************************************************************************/

#ifndef AUTHPOLICY_H_INCLUDED
#define AUTHPOLICY_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "utils/Cooldown.h"
#include "utils/FailureGate.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define AUTH_POLICY_FAILURE_DELAY_MS 500
#define AUTH_POLICY_LOCKOUT_THRESHOLD 5
#define AUTH_POLICY_LOCKOUT_DURATION_MS (30 * 1000)

typedef struct tag_AUTH_POLICY {
    FAILURE_GATE FailureGate;
    COOLDOWN FailureCooldown;
    UINT LockoutEndTime;
    BOOL Initialized;
} AUTH_POLICY, *LPAUTH_POLICY;

/************************************************************************/

BOOL AuthPolicyInit(LPAUTH_POLICY Policy, U32 FailureDelayMS, U32 LockoutThreshold);
void AuthPolicyReset(LPAUTH_POLICY Policy);
BOOL AuthPolicyCanAttempt(LPAUTH_POLICY Policy, UINT Now, UINT* WaitRemainingOut);
BOOL AuthPolicyRecordFailure(LPAUTH_POLICY Policy, UINT Now, UINT* WaitRemainingOut);
void AuthPolicyRecordSuccess(LPAUTH_POLICY Policy);

/************************************************************************/

#pragma pack(pop)

#endif
