// circular_log.c
// Rolling circular log with pattern count stop
// gcc -O2 -o circular_log circular_log.c

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_SIZE 4096

static struct option long_opts[] = {
    {"output", required_argument, 0, 'o'},
    {"size", required_argument, 0, 's'},
    {"string", required_argument, 0, 'S'},
    {"count", required_argument, 0, 'c'},
    {0, 0, 0, 0}};

int main(int argc, char *argv[]) {
    const char *outfile = NULL;
    size_t bufsize = DEFAULT_SIZE;
    const char *pattern = NULL;
    int stop_count = -1;
    int occurrences = 0;
    int stop_writing = 0;

    int opt;
    while ((opt = getopt_long(argc, argv, "o:s:S:c:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'o':
                outfile = optarg;
                break;
            case 's':
                bufsize = strtoul(optarg, NULL, 10);
                break;
            case 'S':
                pattern = optarg;
                break;
            case 'c':
                stop_count = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s -o <file> [-s <size>] [-S <string> -c <count>]\n", argv[0]);
                return 1;
        }
    }

    if (!outfile) {
        fprintf(stderr, "Error: output file is required (use -o)\n");
        return 1;
    }

    int fd = open(outfile, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Allocate rolling buffer
    char *buffer = malloc(bufsize);
    if (!buffer) {
        perror("malloc");
        close(fd);
        return 1;
    }
    size_t buf_len = 0;

    char chunk[1024];
    ssize_t r;

    while ((r = read(STDIN_FILENO, chunk, sizeof(chunk))) > 0) {
        size_t chunk_len = (size_t)r;
        int reached_limit = 0;

        // Count pattern occurrences in this chunk
        if (!stop_writing && pattern && stop_count > 0) {
            char *p = chunk;
            while ((p = strstr(p, pattern)) != NULL) {
                occurrences++;
                p += strlen(pattern);
                if (occurrences >= stop_count) {
                    reached_limit = 1;  // mark only, do not stop writing yet
                }
            }
        }

        // Write chunk into rolling buffer
        if (!stop_writing) {
            if (chunk_len >= bufsize) {
                // Keep only the last bufsize bytes of the chunk
                memcpy(buffer, chunk + (chunk_len - bufsize), bufsize);
                buf_len = bufsize;
            } else {
                // Append chunk to buffer
                memcpy(buffer + buf_len, chunk, chunk_len);
                buf_len += chunk_len;

                // If total length exceeds bufsize, trim from the left
                if (buf_len > bufsize) {
                    size_t overflow = buf_len - bufsize;
                    memmove(buffer, buffer + overflow, bufsize);
                    buf_len = bufsize;
                }
            }

            // Rewrite the file with updated buffer
            if (ftruncate(fd, 0) < 0) {
                perror("ftruncate");
                break;
            }
            if (lseek(fd, 0, SEEK_SET) < 0) {
                perror("lseek");
                break;
            }
            if (write(fd, buffer, buf_len) != (ssize_t)buf_len) {
                perror("write");
                break;
            }
            fsync(fd);
        }

        // After writing, activate stop_writing if limit reached
        if (reached_limit) {
            stop_writing = 1;
            fprintf(
                stderr,
                "Pattern '%s' reached %d occurrences. "
                "Further writes to '%s' are discarded.\n",
                pattern, occurrences, outfile);
        }
    }

    free(buffer);
    close(fd);
    return 0;
}
