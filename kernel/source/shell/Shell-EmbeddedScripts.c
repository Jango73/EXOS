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

static LPCSTR G_DriverListScript = (LPCSTR)
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
    "}\n";

static LPCSTR G_DiskListScript = (LPCSTR)
    "count = storage.count;\n"
    "for (i = 0; i < count; i = i + 1) {\n"
    "    print(\"Manufacturer : \" + storage[i].driver_manufacturer);\n"
    "    print(\"Product      : \" + storage[i].driver_product);\n"
    "    print(\"Sector size  : \" + storage[i].bytes_per_sector);\n"
    "    print(\"Sectors low  : \" + storage[i].num_sectors_low);\n"
    "    print(\"Sectors high : \" + storage[i].num_sectors_high);\n"
    "    print(\"\");\n"
    "}\n";

static LPCSTR G_UsbPortsScript = (LPCSTR)
    "count = usb.ports.count;\n"
    "if (count == 0) {\n"
    "    print(\"No USB port detected\");\n"
    "}\n"
    "for (i = 0; i < count; i = i + 1) {\n"
    "    print(\"port=\" + usb.ports[i].port_number + \" bus=\" + usb.ports[i].bus + \" device=\" + usb.ports[i].device + \" function=\" + usb.ports[i].function + \" speed_id=\" + usb.ports[i].speed_id + \" connected=\" + usb.ports[i].connected + \" enabled=\" + usb.ports[i].enabled + \" status=\" + usb.ports[i].port_status);\n"
    "}\n";

static LPCSTR G_UsbDevicesScript = (LPCSTR)
    "count = usb.devices.count;\n"
    "if (count == 0) {\n"
    "    print(\"No USB device detected\");\n"
    "}\n"
    "for (i = 0; i < count; i = i + 1) {\n"
    "    print(\"addr=\" + usb.devices[i].address + \" bus=\" + usb.devices[i].bus + \" device=\" + usb.devices[i].device + \" function=\" + usb.devices[i].function + \" port=\" + usb.devices[i].port_number + \" speed_id=\" + usb.devices[i].speed_id + \" vid=\" + usb.devices[i].vendor_id + \" pid=\" + usb.devices[i].product_id);\n"
    "}\n";

/************************************************************************/

/**
 * @brief Get the embedded E0 source for `driver list`.
 * @return Embedded script text.
 */
LPCSTR ShellGetEmbeddedDriverListScript(void) {
    return G_DriverListScript;
}

/************************************************************************/

/**
 * @brief Get the embedded E0 source for `disk`.
 * @return Embedded script text.
 */
LPCSTR ShellGetEmbeddedDiskListScript(void) {
    return G_DiskListScript;
}

/************************************************************************/

/**
 * @brief Get the embedded E0 source for `usb ports`.
 * @return Embedded script text.
 */
LPCSTR ShellGetEmbeddedUsbPortsScript(void) {
    return G_UsbPortsScript;
}

/************************************************************************/

/**
 * @brief Get the embedded E0 source for `usb devices`.
 * @return Embedded script text.
 */
LPCSTR ShellGetEmbeddedUsbDevicesScript(void) {
    return G_UsbDevicesScript;
}
