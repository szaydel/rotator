
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/errno.h>
#include <sys/stat.h>
#ifdef __illumos__
#include <errno.h>
#endif

#define BUFSZ 4096
#define MAXFRAGS 10
#define FRAGSZ 5 * 1024 * 1024

int write_with_limit(char *filename, size_t limit) {
    int fd;
    int retcode = 0;
    unsigned buf[BUFSZ] = {0};
    int oflag = O_WRONLY | O_CREAT | O_TRUNC;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    size_t total_written = 0;
    bool opened = false;

    assert(limit > total_written); // This is a requirement.

    while (total_written < limit) {
        ssize_t nwritten = 0;
        ssize_t nread = 0;
        nread = read(STDIN_FILENO, buf, BUFSZ);
        // If we did not read anything from the stream and there is no error,
        // we are therefore at the end of the stream, i.e. EOF.
        if (nread == 0 && errno == 0) {
            retcode = -1;
            goto done;
        }

        if (errno) {
            if (errno == EBADF) { // We reached EOF in this situation.
                retcode = -1;
                goto done;
            }
            if (errno != EINTR && errno != ETIMEDOUT) {
                perror("read failed");
                retcode = 1;
                goto done;
            }
        }

        if (!opened) {
            // If we have something to write out, open the file with given name
            // and write chunks until we are at the limit.
            fd = open(filename, oflag, mode);
            if (fd == -1) {
                perror("open failed");
                retcode = 1;
                goto done;
            }
            opened = true;
        }

        size_t remainder = 4096;
        while (remainder) {
            nwritten = write(fd, buf, nread);
            if (errno) {
                if (errno != EINTR && errno != ETIMEDOUT) {
                    perror("write failed");
                    retcode = 1;
                    goto done;
                }
            }

            if (nwritten == -1) {
                continue;
            }

            remainder = nread - nwritten;
            total_written += nwritten;
        }

    }

    done:
    // Close the file before we return.
    if (fd > 0) close(fd);
    // If we are done reading, i.e. end-of-stream has been reached, we
    // return -1 to let the caller now that we are done.
    return total_written == 0 ? -1 : retcode;
}

int main(int argc, char *argv[]) {
    uint16_t idx = 0;
    char *filename;
    int retcode = 0;
    size_t fragment_size = FRAGSZ;

    if (argc > 1) {
        filename = argv[1];
    } else {
        filename = "output";
        fprintf(stderr, "destination file name not specified; defaulting to'%s.idx'\n", filename);
    }

    size_t limit_len = strlen(filename)+10;
    char filename_dyn[limit_len];

    if(argc > 2) {
        fragment_size = (size_t)atoll(argv[2]);
        if (fragment_size == 0) {
            fprintf(stderr, "failed to parse size of file fragments\n");
            retcode = 1;
            goto done;
        }
    }

    if ((argc > 1) && ((strncasecmp("-h", argv[1], 3) == 0) || (strncasecmp("--help", argv[1], 3) == 0))) {
        fprintf(stderr, "%s [output_file_name] [fragment_file_length]\n", argv[0]);
        retcode = 2;
        goto done;
    }

    if (fragment_size < BUFSZ) {
        fprintf(stderr, "file fragment size '%zu' cannot be less than internal buffer; increasing size to '%d'\n", fragment_size, BUFSZ);
        fragment_size = BUFSZ;
    }

    while (1) {
        // We don't do anything here to name the files. We instead just create
        // a circular buffer of files, with the only downside being that the
        // file with the highest index may not be the one with latest data. But,
        // we have modified timestamps to help us with this, thus it is not a
        // big deal.
        snprintf(filename_dyn, limit_len, "%s.%d", filename, idx);
        retcode = write_with_limit(filename_dyn, fragment_size);
        switch (retcode) {
            case -1:
                goto done;
            case 0:
                idx = (idx + 1) % MAXFRAGS;
                break;
            case 1:
                goto done;
        }
    }

    done:
    return retcode;
}
