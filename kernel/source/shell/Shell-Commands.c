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
    {"commands", "help", "", CMD_commands},
    {"clear", "cls", "", CMD_cls},
    {"con_mode", "mode", "Columns Rows|list", CMD_conmode},
    {"keyboard", "keyboard", "--layout Code", CMD_keyboard},
    {"pause", "pause", "on|off", CMD_pause},
    {"list", "dir", "[Name] [-p] [-r]", CMD_dir},
    {"cd", "cd", "Name", CMD_cd},
    {"mkdir", "md", "Name", CMD_md},
    {"run", "launch", "Name [-b|--background]", CMD_run},
    {"package", "package", "run|list|add ...", CMD_package},
    {"quit", "exit", "", CMD_exit},
    {"sys", "sys_info", "", CMD_sysinfo},
    {"kill", "kill_task", "Number", CMD_killtask},
    {"process", "show_process", "Number", CMD_showprocess},
    {"task", "show_task", "Number", CMD_showtask},
    {"mem", "mem_edit", "Address", CMD_memedit},
    {"dis", "disasm", "Address InstructionCount", CMD_disasm},
    {"memory_map", "memory_map", "", CMD_memorymap},
    {"cat", "type", "", CMD_cat},
    {"cp", "copy", "", CMD_copy},
    {"edit", "edit", "Name", CMD_edit},
    {"disk", "disk", "", CMD_disk},
    {"fs", "file_system", "[--long]", CMD_filesystem},
    {"net", "network", "devices", CMD_network},
    {"pic", "pic", "", CMD_pic},
    {"outp", "outp", "", CMD_outp},
    {"inp", "inp", "", CMD_inp},
    {"reboot", "reboot", "", CMD_reboot},
    {"shutdown", "power_off", "", CMD_shutdown},
    {"add_user", "new_user", "username", CMD_adduser},
    {"del_user", "delete_user", "username", CMD_deluser},
    {"login", "login", "", CMD_login},
    {"logout", "logout", "", CMD_logout},
    {"who_am_i", "who", "", CMD_whoami},
    {"passwd", "set_password", "", CMD_passwd},
    {"prof", "profiling", "", CMD_prof},
    {"autotest", "autotest", "stack", CMD_autotest},
    {"usb", "usb", "ports|devices|tree|drives|probe", CMD_usb},
    {"nvme", "nvme", "list", CMD_nvme},
    {"data", "data_view", "", CMD_dataview},
    {"", "", "", NULL},
};
