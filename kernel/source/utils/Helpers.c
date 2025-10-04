
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


    Helper functions

\************************************************************************/

#include "../include/utils/Helpers.h"
#include "Kernel.h"
#include "SystemFS.h"
#include "Schedule.h"
#include "UserAccount.h"

/***************************************************************************/

LPUSERACCOUNT GetCurrentUser(void) {
    LPPROCESS CurrentProcess = GetCurrentProcess();
    if (CurrentProcess == NULL || CurrentProcess->Session == NULL) {
        return NULL;
    }

    return FindUserAccountByID(CurrentProcess->Session->UserID);
}

/***************************************************************************/

LPTOML GetConfiguration(void) {
    return Kernel.Configuration;
}

/***************************************************************************/

LPFILESYSTEM GetSystemFS(void) {
    return &Kernel.SystemFS.Header;
}

/***************************************************************************/

LPSYSTEMFSFILESYSTEM GetSystemFSFilesystem(void) {
    return &Kernel.SystemFS;
}

/***************************************************************************/

/**
 * @brief Gets a configuration value from the TOML configuration file
 * @param path Path to the configuration value (e.g., "Network.LocalIP")
 * @return String value or NULL if not found
 */
LPCSTR GetConfigurationValue(LPCSTR path) {
    if (Kernel.Configuration == NULL || path == NULL) {
        return NULL;
    }

    return TomlGet(Kernel.Configuration, path);
}
