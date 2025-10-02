
/************************************************************************\

    EXOS Runtime
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


    EXOS STD C API

\************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

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

/************************************************************************/

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline unsigned short htons(unsigned short Value) { return Value; }

static inline unsigned short ntohs(unsigned short Value) { return Value; }

static inline unsigned long htonl(unsigned long Value) { return Value; }

static inline unsigned long ntohl(unsigned long Value) { return Value; }

#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline unsigned short htons(unsigned short Value) { return (unsigned short)((Value << 8) | (Value >> 8)); }

static inline unsigned short ntohs(unsigned short Value) { return htons(Value); }

static inline unsigned long htonl(unsigned long Value) {
    return ((Value & 0x000000FFU) << 24) | ((Value & 0x0000FF00U) << 8) | ((Value & 0x00FF0000U) >> 8) |
           ((Value & 0xFF000000U) >> 24);
}

static inline unsigned long ntohl(unsigned long Value) { return htonl(Value); }

#else
    #error "Endianness not defined"
#endif

#ifndef NULL
#define NULL 0L
#endif

/************************************************************************/
// Types

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef unsigned long size_t;

/************************************************************************/

extern void debug(char* format, ...);

/************************************************************************/

extern unsigned exoscall(unsigned function, unsigned paramter);
extern void __exit__(int code);
extern unsigned strcmp(const char*, const char*);
extern int strncmp(const char*, const char*, unsigned);
extern char* strstr(const char* haystack, const char* needle);
extern char* strchr(const char* string, int character);
extern void memset(void*, int, int);
extern void memcpy(void*, const void*, int);
extern void* memmove(void*, const void*, int);
extern unsigned strlen(const char*);
extern char* strcpy(char*, const char*);

// Command line arguments
extern int _argc;
extern char** _argv;
extern void _SetupArguments(void);

/************************************************************************/

extern void exit(int code);
extern void* malloc(size_t size);
extern void free(void* pointer);
extern int getch(void);
extern int getkey(void);
extern int sprintf(char* str, const char* fmt, ...);
extern int printf(const char* format, ...);
extern int _beginthread(void (*function)(void*), unsigned stack_size, void* arg_list);
extern void _endthread(void);
extern int system(const char*);
extern void sleep(unsigned ms);
extern int atoi(const char* str);

/************************************************************************/

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

/************************************************************************/

typedef long fpos_t;

extern FILE* fopen(const char*, const char*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern size_t fwrite(const void*, size_t, size_t, FILE*);
extern int fprintf(FILE* fp, const char* fmt, ...);
extern int fseek(FILE*, long int, int);
extern long int ftell(FILE*);
extern int fclose(FILE*);
extern int feof(FILE*);
extern int fflush(FILE*);
extern int fgetc(FILE*);

/************************************************************************/
// POSIX Socket interface

typedef unsigned int socklen_t;

#pragma pack(push, 1)
struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int sin_addr;
    unsigned char sin_zero[8];
};
#pragma pack(pop)

// Socket option constants
#define SOL_SOCKET                1
#define SO_RCVTIMEO               20

int     socket(int domain, int type, int protocol);
int     bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int     listen(int sockfd, int backlog);
int     accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
size_t  send(int sockfd, const void *buf, size_t len, int flags);
size_t  recv(int sockfd, void *buf, size_t len, int flags);
size_t  sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
size_t  recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int     shutdown(int sockfd, int how);
int     getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int     setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int     getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif
