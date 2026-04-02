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
    [SHELL_EMBEDDED_SCRIPT_TASK_LIST] = (LPCSTR)
        "count = task.count;\n"
        "if (count == 0) {\n"
        "    print(\"No task detected\");\n"
        "}\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(\"task=\" + i + \" name=\" + task[i].name + \" type=\" + task[i].type + \" status=\" + task[i].status + \" priority=\" + task[i].priority + \" flags=\" + task[i].flags + \" exit=\" + task[i].exit_code);\n"
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
    [SHELL_EMBEDDED_SCRIPT_FILE_SYSTEM_LIST] = (LPCSTR)
        "print(\"Name         Type         Format           Sectors\");\n"
        "print(\"-------------------------------------------------\");\n"
        "unmounted = 0;\n"
        "count = file_system.mounted.count;\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(file_system.mounted[i].name + \" \" + file_system.mounted[i].type_name + \" \" + file_system.mounted[i].format_name + \" \" + file_system.mounted[i].num_sectors);\n"
        "}\n"
        "count = file_system.unused.count;\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(file_system.unused[i].name + \"* \" + file_system.unused[i].type_name + \" \" + file_system.unused[i].format_name + \" \" + file_system.unused[i].num_sectors);\n"
        "    unmounted = unmounted + 1;\n"
        "}\n"
        "if (unmounted > 0) {\n"
        "    print(\"\");\n"
        "    print(\"* = unmounted\");\n"
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_FILE_SYSTEM_LIST_LONG] = (LPCSTR)
        "active = file_system.active_partition_name;\n"
        "print(\"General information\");\n"
        "if (active == \"\") {\n"
        "    print(\"Active partition : <none>\");\n"
        "} else {\n"
        "    print(\"Active partition : \" + active);\n"
        "}\n"
        "print(\"\");\n"
        "print(\"Discovered file systems\");\n"
        "count = file_system.mounted.count;\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(\"Name         : \" + file_system.mounted[i].name);\n"
        "    if (file_system.mounted[i].mounted != 0) {\n"
        "        print(\"Mounted      : YES\");\n"
        "    } else {\n"
        "        print(\"Mounted      : NO\");\n"
        "    }\n"
        "    if (file_system.mounted[i].driver_product == \"\") {\n"
        "        print(\"FS driver    : <none>\");\n"
        "    } else {\n"
        "        print(\"FS driver    : \" + file_system.mounted[i].driver_manufacturer + \" / \" + file_system.mounted[i].driver_product);\n"
        "    }\n"
        "    print(\"Scheme       : \" + file_system.mounted[i].scheme_name);\n"
        "    print(\"Type         : \" + file_system.mounted[i].type_name);\n"
        "    print(\"Format       : \" + file_system.mounted[i].format_name);\n"
        "    print(\"Index        : \" + file_system.mounted[i].index);\n"
        "    print(\"Start sector : \" + file_system.mounted[i].start_sector);\n"
        "    print(\"Size         : \" + file_system.mounted[i].num_sectors + \" sectors\");\n"
        "    if ((file_system.mounted[i].flags & 1) != 0) {\n"
        "        print(\"Active       : YES\");\n"
        "    } else {\n"
        "        print(\"Active       : NO\");\n"
        "    }\n"
        "    if (file_system.mounted[i].scheme == 1) {\n"
        "        print(\"Type id      : \" + file_system.mounted[i].type);\n"
        "    }\n"
        "    if (file_system.mounted[i].has_storage != 0) {\n"
        "        print(\"Storage      : \" + file_system.mounted[i].storage_manufacturer + \" / \" + file_system.mounted[i].storage_product);\n"
        "        if (file_system.mounted[i].removable != 0) {\n"
        "            print(\"Removable    : YES\");\n"
        "        } else {\n"
        "            print(\"Removable    : NO\");\n"
        "        }\n"
        "        if (file_system.mounted[i].read_only != 0) {\n"
        "            print(\"Read only    : YES\");\n"
        "        } else {\n"
        "            print(\"Read only    : NO\");\n"
        "        }\n"
        "        print(\"Disk sectors : \" + file_system.mounted[i].disk_num_sectors_high + \", \" + file_system.mounted[i].disk_num_sectors_low);\n"
        "    } else {\n"
        "        print(\"Storage      : <none>\");\n"
        "    }\n"
        "    print(\"\");\n"
        "}\n"
        "count = file_system.unused.count;\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    print(\"Name         : \" + file_system.unused[i].name);\n"
        "    if (file_system.unused[i].mounted != 0) {\n"
        "        print(\"Mounted      : YES\");\n"
        "    } else {\n"
        "        print(\"Mounted      : NO\");\n"
        "    }\n"
        "    if (file_system.unused[i].driver_product == \"\") {\n"
        "        print(\"FS driver    : <none>\");\n"
        "    } else {\n"
        "        print(\"FS driver    : \" + file_system.unused[i].driver_manufacturer + \" / \" + file_system.unused[i].driver_product);\n"
        "    }\n"
        "    print(\"Scheme       : \" + file_system.unused[i].scheme_name);\n"
        "    print(\"Type         : \" + file_system.unused[i].type_name);\n"
        "    print(\"Format       : \" + file_system.unused[i].format_name);\n"
        "    print(\"Index        : \" + file_system.unused[i].index);\n"
        "    print(\"Start sector : \" + file_system.unused[i].start_sector);\n"
        "    print(\"Size         : \" + file_system.unused[i].num_sectors + \" sectors\");\n"
        "    if ((file_system.unused[i].flags & 1) != 0) {\n"
        "        print(\"Active       : YES\");\n"
        "    } else {\n"
        "        print(\"Active       : NO\");\n"
        "    }\n"
        "    if (file_system.unused[i].scheme == 1) {\n"
        "        print(\"Type id      : \" + file_system.unused[i].type);\n"
        "    }\n"
        "    if (file_system.unused[i].has_storage != 0) {\n"
        "        print(\"Storage      : \" + file_system.unused[i].storage_manufacturer + \" / \" + file_system.unused[i].storage_product);\n"
        "        if (file_system.unused[i].removable != 0) {\n"
        "            print(\"Removable    : YES\");\n"
        "        } else {\n"
        "            print(\"Removable    : NO\");\n"
        "        }\n"
        "        if (file_system.unused[i].read_only != 0) {\n"
        "            print(\"Read only    : YES\");\n"
        "        } else {\n"
        "            print(\"Read only    : NO\");\n"
        "        }\n"
        "        print(\"Disk sectors : \" + file_system.unused[i].disk_num_sectors_high + \", \" + file_system.unused[i].disk_num_sectors_low);\n"
        "    } else {\n"
        "        print(\"Storage      : <none>\");\n"
        "    }\n"
        "    print(\"\");\n"
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_MEMORY_MAP] = (LPCSTR)
        "count = memory_map.kernel_regions.count;\n"
        "print(\"Kernel regions: \" + count);\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    line = \"\" + i + \": tag=\" + memory_map.kernel_regions[i].tag + \" base=(\" + memory_map.kernel_regions[i].base_high + \",\" + memory_map.kernel_regions[i].base_low + \") size=\" + memory_map.kernel_regions[i].size;\n"
        "    if (memory_map.kernel_regions[i].physical_known != 0) {\n"
        "        line = line + \" phys=(\" + memory_map.kernel_regions[i].physical_high + \",\" + memory_map.kernel_regions[i].physical_low + \")\";\n"
        "    } else {\n"
        "        line = line + \" phys=?\";\n"
        "    }\n"
        "    print(line);\n"
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
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_USB_DEVICE_TREE] = (LPCSTR)
        "count = usb.nodes.count;\n"
        "if (count == 0) {\n"
        "    print(\"No USB device tree detected\");\n"
        "}\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    if (usb.nodes[i].node_type == 1) {\n"
        "        print(\"Device Port \" + usb.nodes[i].port_number + \" Addr \" + usb.nodes[i].address + \" VID=\" + usb.nodes[i].vendor_id + \" PID=\" + usb.nodes[i].product_id + \" Class=\" + usb.nodes[i].device_class + \"/\" + usb.nodes[i].device_sub_class + \"/\" + usb.nodes[i].device_protocol + \" Speed=\" + usb.nodes[i].speed_id);\n"
        "    } else if (usb.nodes[i].node_type == 2) {\n"
        "        print(\"  Config \" + usb.nodes[i].config_value + \" Attr=\" + usb.nodes[i].config_attributes + \" MaxPower=\" + usb.nodes[i].config_max_power);\n"
        "    } else if (usb.nodes[i].node_type == 3) {\n"
        "        print(\"    Interface \" + usb.nodes[i].interface_number + \" Alt=\" + usb.nodes[i].alternate_setting + \" Class=\" + usb.nodes[i].interface_class + \"/\" + usb.nodes[i].interface_sub_class + \"/\" + usb.nodes[i].interface_protocol);\n"
        "    } else if (usb.nodes[i].node_type == 4) {\n"
        "        direction = \"OUT\";\n"
        "        if ((usb.nodes[i].endpoint_address & 128) != 0) {\n"
        "            direction = \"IN\";\n"
        "        }\n"
        "        print(\"      Endpoint \" + usb.nodes[i].endpoint_address + \" \" + direction + \" Attr=\" + usb.nodes[i].endpoint_attributes + \" MaxPacket=\" + usb.nodes[i].endpoint_max_packet_size + \" Interval=\" + usb.nodes[i].endpoint_interval);\n"
        "    }\n"
        "}\n",
    [SHELL_EMBEDDED_SCRIPT_USB_PROBE] = (LPCSTR)
        "count = usb.ports.count;\n"
        "if (count == 0) {\n"
        "    print(\"No xHCI controller detected\");\n"
        "}\n"
        "for (i = 0; i < count; i = i + 1) {\n"
        "    if (usb.ports[i].connected != 0) {\n"
        "        line = \"P\" + usb.ports[i].port_number + \" Err=\" + usb.ports[i].last_enum_error_text;\n"
        "        if (usb.ports[i].last_enum_error == 5) {\n"
        "            line = line + \" C=\" + usb.ports[i].last_enum_completion;\n"
        "        }\n"
        "        print(line);\n"
        "    }\n"
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
