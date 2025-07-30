/***************************************************************************\

  EXOS Run-Time Library
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "exos-runtime.h"
#include "../../kernel/source/User.h"

/***************************************************************************/

extern unsigned exoscall (unsigned, unsigned);
extern void     __exit__ (int);

/***************************************************************************/

void exit (int ErrorCode)
{
  __exit__(ErrorCode);
}

/***************************************************************************/

void* malloc (size_t s)
{
  return (void*) exoscall(SYSCALL_HeapAlloc, s);
}

/***************************************************************************/

void free (void* p)
{
  exoscall(SYSCALL_HeapFree, (unsigned) p);
}

/***************************************************************************/

int getch ()
{
  KEYCODE KeyCode;

  while (exoscall(SYSCALL_ConsolePeekKey, 0) == 0) {}

  exoscall(SYSCALL_ConsoleGetKey, (U32) &KeyCode);

  return (int) KeyCode.ASCIICode;
}

/***************************************************************************/

int printf (const char* fmt, ...)
{
  return (int) exoscall(SYSCALL_ConsolePrint, (unsigned) fmt);
}

/***************************************************************************/

int _beginthread
(
  void (*__start_address) (void*),
  void* __stack_bottom,
  unsigned __stack_size,
  void* __arglist
)
{
  TASKINFO TaskInfo;

  TaskInfo.Func      = (TASKFUNC) __start_address;
  TaskInfo.Parameter = (LPVOID) __arglist;
  TaskInfo.StackSize = (U32) __stack_size;
  TaskInfo.Priority  = TASK_PRIORITY_MEDIUM;
  TaskInfo.Flags     = 0;

  return (int) exoscall(SYSCALL_CreateTask, (unsigned) &TaskInfo);
}

/***************************************************************************/

void _endthread ()
{
}

/***************************************************************************/

int system (const char* __cmd)
{
  PROCESSINFO ProcessInfo;

  ProcessInfo.FileName = NULL;
  ProcessInfo.CommandLine = __cmd;
  ProcessInfo.Flags  = 0;
  ProcessInfo.StdOut = NULL;
  ProcessInfo.StdIn  = NULL;
  ProcessInfo.StdErr = NULL;

  return (int) exoscall(SYSCALL_CreateProcess, (U32) &ProcessInfo);
}

/***************************************************************************/

FILE* fopen (const char* __name, const char* __mode)
{
  FILEOPENINFO info;
  FILE* __fp;
  HANDLE handle;

  handle = exoscall(SYSCALL_OpenFile, (unsigned) &info);

  if (handle)
  {
    __fp = (FILE*) malloc(sizeof(FILE));

    if (__fp == NULL)
    {
      exoscall(SYSCALL_DeleteObject, handle);
      return NULL;
    }

    __fp->_ptr      = NULL;
    __fp->_cnt      = 0;
    __fp->_base     = (unsigned char*) malloc(4096);
    __fp->_flag     = 0;
    __fp->_handle   = handle;
    __fp->_bufsize  = 4096;
    __fp->_ungotten = 0;
    __fp->_tmpfchar = 0;

    return __fp;
  }

  return NULL;
}

/***************************************************************************/

int fclose (FILE* __fp)
{
  if (__fp)
  {
    exoscall(SYSCALL_DeleteObject, __fp->_handle);
    if (__fp->_base) free(__fp->_base);
    free(__fp);
    return 1;
  }
  return 0;
}

/***************************************************************************/

size_t fread (void* __buf, size_t __elsize, size_t __num, FILE* __fp)
{
  FILEOPERATION fileop;

  if (!__fp) return 0;

  fileop.Size     = sizeof fileop;
  fileop.File     = (HANDLE) __fp->_handle;
  fileop.NumBytes = __elsize * __num;
  fileop.Buffer   = __buf;

  return (size_t) exoscall(SYSCALL_ReadFile, (unsigned) &fileop);
}

/***************************************************************************/

size_t fwrite (const void* __buf, size_t __elsize, size_t __num, FILE* __fp)
{
}

/***************************************************************************/

int fseek (FILE* __fp, long int __pos, int __whence)
{
}

/***************************************************************************/

long int ftell (FILE* __fp)
{
}

/***************************************************************************/

int feof (FILE* __fp)
{
}

/***************************************************************************/

int fflush (FILE* __fp)
{
}

/***************************************************************************/

int fgetc (FILE* __fp)
{
}

/***************************************************************************/
