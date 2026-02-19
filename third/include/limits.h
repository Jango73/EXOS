#ifndef limits_h
#define limits_h

#define CHAR_BIT 8

#if defined(__EXOS_32__)
#define ULONG_MAX 0xFFFFFFFF
#else
#define ULONG_MAX 0xFFFFFFFFFFFFFFFF
#endif

#endif  // limits_h
