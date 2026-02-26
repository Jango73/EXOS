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


    Shell commands - command table

\************************************************************************/

#include "shell/Shell-Commands-Private.h"

/************************************************************************/

SHELL_COMMAND_ENTRY COMMANDS[] = {
    {"add_user", "new_user", "username", CMD_adduser},
    {"autotest", "autotest", "stack", CMD_autotest},
    {"cat", "type", "", CMD_cat},
    {"cd", "cd", "Name", CMD_cd},
    {"clear", "cls", "", CMD_cls},
    {"commands", "help", "", CMD_commands},
    {"con_mode", "mode", "Columns Rows|list", CMD_conmode},
    {"cp", "copy", "", CMD_copy},
    {"data", "data_view", "", CMD_dataview},
    {"del_user", "delete_user", "username", CMD_deluser},
    {"dis", "disasm", "Address InstructionCount", CMD_disasm},
    {"disk", "disk", "", CMD_disk},
    {"driver", "driver", "Alias", CMD_driver},
    {"edit", "edit", "Name", CMD_edit},
    {"fs", "file_system", "[--long]", CMD_filesystem},
    {"gfx", "graphics", "backend Driver Mode|smoke_test [DurationMs]", CMD_gfx},
    {"inp", "inp", "", CMD_inp},
    {"keyboard", "keyboard", "--layout Code", CMD_keyboard},
    {"kill", "kill_task", "Number", CMD_killtask},
    {"list", "dir", "[Name] [-p] [-r]", CMD_dir},
    {"login", "login", "", CMD_login},
    {"logout", "logout", "", CMD_logout},
    {"mem", "mem_edit", "Address", CMD_memedit},
    {"memory_map", "memory_map", "", CMD_memorymap},
    {"mkdir", "md", "Name", CMD_md},
    {"net", "network", "devices", CMD_network},
    {"nvme", "nvme", "list", CMD_nvme},
    {"outp", "outp", "", CMD_outp},
    {"package", "package", "run|list|add ...", CMD_package},
    {"passwd", "set_password", "", CMD_passwd},
    {"pause", "pause", "on|off", CMD_pause},
    {"pic", "pic", "", CMD_pic},
    {"process", "show_process", "Number", CMD_showprocess},
    {"prof", "profiling", "", CMD_prof},
    {"quit", "exit", "", CMD_exit},
    {"reboot", "reboot", "", CMD_reboot},
    {"run", "launch", "Name [-b|--background]", CMD_run},
    {"shutdown", "power_off", "", CMD_shutdown},
    {"sys", "sys_info", "", CMD_sysinfo},
    {"task", "show_task", "Number", CMD_showtask},
    {"usb", "usb", "ports|devices|tree|drives|probe", CMD_usb},
    {"who_am_i", "who", "", CMD_whoami},
    {"", "", "", NULL},
};
