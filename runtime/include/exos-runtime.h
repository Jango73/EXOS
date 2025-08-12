
/***************************************************************************/

// ANSI required limits

#define CHAR_BIT 8

#ifdef __CHAR_SIGNED__
#define CHAR_MIN (-128)
#define CHAR_MAX 127
#else
#define CHAR_MIN 0
#define CHAR_MAX 255
#endif

#define MB_LEN_MAX 2
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255U

#define SHRT_MIN (-32767 - 1)
#define SHRT_MAX 32767
#define USHRT_MAX 65535U
#define LONG_MAX 2147483647L
#define LONG_MIN (-2147483647L - 1)
#define ULONG_MAX 4294967295UL

#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U

#define TZNAME_MAX 30

/***************************************************************************/

#ifndef NULL
#define NULL 0L
#endif

/***************************************************************************/

typedef unsigned long size_t;

/***************************************************************************/

extern void exit(int);
extern void* malloc(size_t);
extern void free(void*);
extern int getch();
extern int printf(const char*, ...);

extern int _beginthread(unsigned long (*start_address)(void*), unsigned stack_size, void* arg_list);
extern void _endthread();
extern int system(const char*);

/***************************************************************************/

typedef struct __iobuf {
    unsigned char* _ptr;     /* next character position */
    int _cnt;                /* number of characters left */
    unsigned char* _base;    /* location of buffer */
    unsigned _flag;          /* mode of file access */
    unsigned _handle;        /* file handle */
    unsigned _bufsize;       /* size of buffer */
    unsigned char _ungotten; /* character placed here by ungetc */
    unsigned char _tmpfchar; /* tmpfile number */
} FILE;

/***************************************************************************/

typedef long fpos_t;

extern FILE* fopen(const char*, const char*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern size_t fwrite(const void*, size_t, size_t, FILE*);
extern int fseek(FILE*, long int, int);
extern long int ftell(FILE*);
extern int fclose(FILE*);
extern int feof(FILE*);
extern int fflush(FILE*);
extern int fgetc(FILE*);

/***************************************************************************/

/*
HANDLE CreateWindow     (HANDLE, LPVOID, U32, U32, U32, U32, U32);
BOOL   DeleteWindow     (HANDLE);
BOOL   ShowWindow       (HANDLE, BOOL);
BOOL   GetTaskMessage   (LPVOID, U32);
BOOL   GetWindowMessage (HANDLE, LPVOID, U32);
*/

/***************************************************************************/
