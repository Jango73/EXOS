
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


    EXOS Runtime

\************************************************************************/

#include "../../kernel/include/User.h"
#include "../include/exos-runtime.h"

/***************************************************************************/

// User-facing structures now start with an ABI_HDR instead of a simple size
// field. Always populate Hdr.Size with sizeof(struct), set Hdr.Version to
// EXOS_ABI_VERSION, and clear Hdr.Flags before invoking system calls.

void exit(int ErrorCode) { __exit__(ErrorCode); }

/***************************************************************************/

void* malloc(size_t s) { return (void*)exoscall(SYSCALL_HeapAlloc, s); }

/***************************************************************************/

void free(void* p) { exoscall(SYSCALL_HeapFree, (unsigned)p); }

/***************************************************************************/

int getch(void) {
    KEYCODE KeyCode;

    while (exoscall(SYSCALL_ConsolePeekKey, 0) == 0) {
    }

    exoscall(SYSCALL_ConsoleGetKey, (U32)&KeyCode);

    return (int)KeyCode.ASCIICode;
}

/***************************************************************************/

int printf(const char* fmt, ...) { return (int)exoscall(SYSCALL_ConsolePrint, (unsigned)fmt); }

/***************************************************************************/

int _beginthread(void (*start_address)(void*), unsigned stack_size, void* arg_list) {
    TASKINFO TaskInfo;

    memset(&TaskInfo, 0, sizeof(TaskInfo));

    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = (TASKFUNC)start_address;
    TaskInfo.Parameter = (LPVOID)arg_list;
    TaskInfo.StackSize = (U32)stack_size;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = 0;

    return (int)exoscall(SYSCALL_CreateTask, (unsigned)&TaskInfo);
}

/***************************************************************************/

void _endthread(void) {}

/***************************************************************************/

int system(const char* __cmd) {
    PROCESSINFO ProcessInfo;

    ProcessInfo.Header.Size = sizeof(PROCESSINFO);
    ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    ProcessInfo.Header.Flags = 0;
    ProcessInfo.FileName = NULL;
    ProcessInfo.CommandLine = (LPCSTR)__cmd;
    ProcessInfo.Flags = 0;
    ProcessInfo.StdOut = NULL;
    ProcessInfo.StdIn = NULL;
    ProcessInfo.StdErr = NULL;

    return (int)exoscall(SYSCALL_CreateProcess, (U32)&ProcessInfo);
}

/***************************************************************************/

FILE* fopen(const char* __name, const char* __mode) {
    FILEOPENINFO info;
    FILE* __fp;
    HANDLE handle;

    info.Header.Size = sizeof(FILEOPENINFO);
    info.Header.Version = EXOS_ABI_VERSION;
    info.Header.Flags = 0;
    info.Flags = 0;
    info.Name = (LPCSTR)__name;
    info.Flags = 0;

    if (strstr(__mode, "r+")) {
        info.Flags |= FILE_OPEN_READ | FILE_OPEN_WRITE | FILE_OPEN_EXISTING;
    } else if (strstr(__mode, "r")) {
        info.Flags |= FILE_OPEN_READ | FILE_OPEN_EXISTING;
    } else if (strstr(__mode, "w+")) {
        info.Flags |= FILE_OPEN_READ | FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;
    } else if (strstr(__mode, "w")) {
        info.Flags |= FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;
    } else if (strstr(__mode, "a+")) {
        info.Flags |= FILE_OPEN_READ | FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_SEEK_END;
    } else if (strstr(__mode, "a")) {
        info.Flags |= FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_SEEK_END;
    } else {
        // Unknown mode
        return NULL;
    }

    handle = exoscall(SYSCALL_OpenFile, (unsigned)&info);

    if (handle) {
        __fp = (FILE*)malloc(sizeof(FILE));

        if (__fp == NULL) {
            exoscall(SYSCALL_DeleteObject, handle);
            return NULL;
        }

        __fp->_ptr = NULL;
        __fp->_cnt = 0;
        __fp->_base = (unsigned char*)malloc(4096);
        __fp->_flag = 0;
        __fp->_handle = handle;
        __fp->_bufsize = 4096;
        __fp->_ungotten = 0;
        __fp->_tmpfchar = 0;

        return __fp;
    }

    return NULL;
}

/***************************************************************************/

int fclose(FILE* __fp) {
    if (__fp) {
        exoscall(SYSCALL_DeleteObject, __fp->_handle);
        if (__fp->_base) free(__fp->_base);
        free(__fp);
        return 1;
    }
    return 0;
}

/***************************************************************************/

size_t fread(void* buf, size_t elsize, size_t num, FILE* fp) {
    FILEOPERATION fileop;

    if (!fp) return 0;

    fileop.Header.Size = sizeof fileop;
    fileop.Header.Version = EXOS_ABI_VERSION;
    fileop.Header.Flags = 0;
    fileop.File = (HANDLE)fp->_handle;
    fileop.NumBytes = elsize * num;
    fileop.Buffer = buf;

    return (size_t)exoscall(SYSCALL_ReadFile, (unsigned)&fileop);
}

/***************************************************************************/

size_t fwrite(const void* buf, size_t elsize, size_t num, FILE* fp) {
    UNUSED(buf);
    UNUSED(elsize);
    UNUSED(num);
    UNUSED(fp);
    return 0;
}

/***************************************************************************/

int fseek(FILE* fp, long int pos, int whence) {
    UNUSED(fp);
    UNUSED(pos);
    UNUSED(whence);
    return 0;
}

/***************************************************************************/

long int ftell(FILE* fp) {
    UNUSED(fp);
    return 0;
}

/***************************************************************************/

int feof(FILE* fp) {
    UNUSED(fp);
    return 0;
}

/***************************************************************************/

int fflush(FILE* fp) {
    UNUSED(fp);
    return 0;
}

/***************************************************************************/

int fgetc(FILE* fp) {
    UNUSED(fp);
    return 0;
}

/***************************************************************************/
