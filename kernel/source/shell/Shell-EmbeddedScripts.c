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


    Shell - Embedded E0 Scripts

\************************************************************************/

#include "shell/Shell-EmbeddedScripts.h"

/************************************************************************/

static LPCSTR G_EmbeddedScripts[SHELL_EMBEDDED_SCRIPT_COUNT] = {
    [SHELL_EMBEDDED_SCRIPT_DRIVER_LIST] = (LPCSTR)
        "count = drivers.count;\n"
        "if (count == 0) {\n"
        "    print(\"No driver detected\");\n"
        "}\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    alias = drivers[i].alias;\n"
        "    if (alias == \"\") {\n"
        "        alias = \"<none>\";\n"
        "    }\n"
        "    product = drivers[i].product;\n"
        "    if (product == \"\") {\n"
        "        product = \"<none>\";\n"
        "    }\n"
        "    print(alias + \" type=\" + drivers[i].type_name + \" ready=\" + drivers[i].ready + \" product=\" + product);\n"
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_DISK_LIST] = (LPCSTR)
        "count = storage.count;\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(\"Manufacturer : \" + storage[i].driver_manufacturer);\n"
        "    print(\"Product      : \" + storage[i].driver_product);\n"
        "    print(\"Sector size  : \" + storage[i].bytes_per_sector);\n"
        "    print(\"Sectors low  : \" + storage[i].num_sectors_low);\n"
        "    print(\"Sectors high : \" + storage[i].num_sectors_high);\n"
        "    print(\"\");\n"
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_NETWORK_DEVICES] = (LPCSTR)
        "count = network.devices.count;\n"
        "if (count == 0) {\n"
        "    print(\"No network device detected\");\n"
        "}\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(\"Name         : \" + network.devices[i].name);\n"
        "    print(\"Manufacturer : \" + network.devices[i].manufacturer);\n"
        "    print(\"Product      : \" + network.devices[i].product);\n"
        "    print(\"MAC          : \" + network.devices[i].mac_0 + \":\" + network.devices[i].mac_1 + \":\" + network.devices[i].mac_2 + \":\" + network.devices[i].mac_3 + \":\" + network.devices[i].mac_4 + \":\" + network.devices[i].mac_5);\n"
        "    print(\"IP Address   : \" + network.devices[i].ip_0 + \".\" + network.devices[i].ip_1 + \".\" + network.devices[i].ip_2 + \".\" + network.devices[i].ip_3);\n"
        "    if (network.devices[i].link_up != 0) {\n"
        "        print(\"Link         : UP\");\n"
        "    } else {\n"
        "        print(\"Link         : DOWN\");\n"
        "    }\n"
        "    print(\"Speed        : \" + network.devices[i].speed_mbps + \" Mbps\");\n"
        "    if (network.devices[i].duplex_full != 0) {\n"
        "        print(\"Duplex       : FULL\");\n"
        "    } else {\n"
        "        print(\"Duplex       : HALF\");\n"
        "    }\n"
        "    print(\"MTU          : \" + network.devices[i].mtu);\n"
        "    if (network.devices[i].initialized != 0) {\n"
        "        print(\"Initialized  : YES\");\n"
        "    } else {\n"
        "        print(\"Initialized  : NO\");\n"
        "    }\n"
        "    print(\"\");\n"
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_USB_PORTS] = (LPCSTR)
        "count = usb.ports.count;\n"
        "if (count == 0) {\n"
        "    print(\"No USB port detected\");\n"
        "}\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(\"port=\" + usb.ports[i].port_number + \" bus=\" + usb.ports[i].bus + \" device=\" + usb.ports[i].device + \" function=\" + usb.ports[i].function + \" speed_id=\" + usb.ports[i].speed_id + \" connected=\" + usb.ports[i].connected + \" enabled=\" + usb.ports[i].enabled + \" status=\" + usb.ports[i].port_status);\n"
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_USB_DEVICES] = (LPCSTR)
        "count = usb.devices.count;\n"
        "if (count == 0) {\n"
        "    print(\"No USB device detected\");\n"
        "}\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(\"addr=\" + usb.devices[i].address + \" bus=\" + usb.devices[i].bus + \" device=\" + usb.devices[i].device + \" function=\" + usb.devices[i].function + \" port=\" + usb.devices[i].port_number + \" speed_id=\" + usb.devices[i].speed_id + \" vid=\" + usb.devices[i].vendor_id + \" pid=\" + usb.devices[i].product_id);\n"
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_USB_DRIVES] = (LPCSTR)
        "count = usb.drives.count;\n"
        "if (count == 0) {\n"
        "    print(\"No USB drive detected\");\n"
        "}\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    state = \"offline\";\n"
        "    if (usb.drives[i].present != 0) {\n"
        "        state = \"online\";\n"
        "    }\n"
        "    print(\"usb\" + i + \": addr=\" + usb.drives[i].address + \" vid=\" + usb.drives[i].vendor_id + \" pid=\" + usb.drives[i].product_id + \" blocks=\" + usb.drives[i].block_count + \" block_size=\" + usb.drives[i].block_size + \" state=\" + state);\n"
        "}\n"
};

/************************************************************************/

/**
 * @brief Get one embedded E0 source by identifier.
 * @param ScriptId Embedded script identifier.
 * @return Embedded script text.
 */
LPCSTR ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_ID ScriptId) {
    if ((UINT)ScriptId >= SHELL_EMBEDDED_SCRIPT_COUNT) {
        return NULL;
    }

    return G_EmbeddedScripts[ScriptId];
}
