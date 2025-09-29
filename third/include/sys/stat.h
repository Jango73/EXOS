/* Kernel stub for sys/stat.h */
#ifndef SYS_STAT_H
#define SYS_STAT_H

#include <sys/types.h>

/* File type flags */
#define S_IFMT 0170000  /* bit mask for the file type bit field */
#define S_IFREG 0100000 /* regular file */

/* File type test macros */
#define S_ISREG(m) (((m)&S_IFMT) == S_IFREG)

/* Basic stat structure for bcrypt */
struct stat {
    mode_t st_mode;  /* file mode */
    off_t st_size;   /* file size */
    time_t st_mtime; /* time of last modification */
};

#endif /* SYS_STAT_H */