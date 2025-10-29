
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


    Helper functions definitions

\************************************************************************/

#ifndef HELPERS_H_INCLUDED
#define HELPERS_H_INCLUDED

/***************************************************************************/

#include "UserAccount.h"
#include "TOML.h"
#include "FileSystem.h"
#include "SystemFS.h"

/***************************************************************************/
// Configuration paths

#define CONFIG_NETWORK_LOCAL_IP     "Network.LocalIP"
#define CONFIG_NETWORK_NETMASK      "Network.Netmask"
#define CONFIG_NETWORK_GATEWAY      "Network.Gateway"
#define CONFIG_NETWORK_DEFAULT_PORT "Network.DefaultPort"
#define CONFIG_NETWORK_USE_DHCP     "Network.UseDHCP"
#define CONFIG_TCP_EPHEMERAL_START      "TCP.EphemeralPortStart"
#define CONFIG_TCP_SEND_BUFFER_SIZE     "TCP.SendBufferSize"
#define CONFIG_TCP_RECEIVE_BUFFER_SIZE  "TCP.ReceiveBufferSize"
#define CONFIG_TASK_MINIMUM_TASK_STACK_SIZE   "Task.MinimumTaskStackSize"
#define CONFIG_TASK_MINIMUM_SYSTEM_STACK_SIZE "Task.MinimumSystemStackSize"

// Per-device network interface configuration (format strings for dynamic paths)
#define CONFIG_NETWORK_INTERFACE_DEVICE_NAME_FMT  "NetworkInterface.%u.DeviceName"
#define CONFIG_NETWORK_INTERFACE_CONFIG_FMT       "NetworkInterface.%u.%s"

// System paths
#define PATH_USERS_DATABASE "/system/data/users.database"

/***************************************************************************/

LPUSERACCOUNT GetCurrentUser(void);
LPTOML GetConfiguration(void);
LPFILESYSTEM GetSystemFS(void);
LPSYSTEMFSFILESYSTEM GetSystemFSFilesystem(void);
LPCSTR GetConfigurationValue(LPCSTR path);

/***************************************************************************/

#endif
