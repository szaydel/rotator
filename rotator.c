#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>

#if defined(__sun) || defined(__illumos__)
// Include illumos/Sun-specific bits here
#endif

#define BUFSZ 4096
#define MAXFRAGS 10
#define FRAGSZ 5 * 1024 * 1024

#define DEFAULT_OUTPUT_FILENAME "output"
#define FRAGMENT_SIZE "FRAGMENT_SIZE"
#define STDIN_NO_DATA_TIMEOUT "STDIN_NO_DATA_TIMEOUT"

// write_with_limit writes up to limit bytes consumed from STDIN into the file
// pointed to by filename.
int
write_with_limit(char* filename, size_t limit)
{
  int fd;
  int retcode = 0;
  unsigned buf[BUFSZ] = { 0 };
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
  if (fd > 0)
    close(fd);
  // If we are done reading, i.e. end-of-stream has been reached, we
  // return -1 to let the caller now that we are done.
  return total_written == 0 ? -1 : retcode;
}

int
main(int argc, char* argv[])
{
  uint16_t idx = 0;
  char* filename;
  int retcode = 0;
  size_t fragment_size = FRAGSZ;
  int stdin_no_data_timeout = 0;

  char* stdin_no_data_timeout_env = NULL;
  char* fragment_size_env = NULL;

  if ((stdin_no_data_timeout_env = getenv(STDIN_NO_DATA_TIMEOUT)) != NULL) {
    int v = atoi(stdin_no_data_timeout_env);

    if (v > 0)
      stdin_no_data_timeout = v;
    fprintf(stderr,
            "setting 'stdin_no_data_timeout' to: %d seconds\n",
            stdin_no_data_timeout);
  }

  if (argc > 1) {
    filename = argv[1];
  } else {
    filename = DEFAULT_OUTPUT_FILENAME;
    fprintf(stderr,
            "destination file name not specified; defaulting to'%s.idx'\n",
            filename);
  }

  size_t limit_len = strlen(filename) + 10;
  char filename_dyn[limit_len];

  if ((fragment_size_env = getenv(FRAGMENT_SIZE)) != NULL) {
    size_t v = (size_t)atoll(fragment_size_env);

    if (v > 0)
      fragment_size = v;
    fprintf(stderr, "setting 'fragment_size' to: %zu bytes\n", fragment_size);
  }

  //   if (argc > 2) {
  //     fragment_size = (size_t)atoll(argv[2]);
  //     if (fragment_size == 0) {
  //       fprintf(stderr, "failed to parse size of file fragments\n");
  //       retcode = 1;
  //       goto done;
  //     }
  //   }

  if ((argc > 1) && ((strncasecmp("-h", argv[1], 3) == 0) ||
                     (strncasecmp("--help", argv[1], 3) == 0))) {
    fprintf(stderr, "Usage: %s [output_filename]\n", argv[0]);
    fprintf(stderr,
            "\nIf output filename is missing, a default '%s.[0..n]' will be used\n",
            DEFAULT_OUTPUT_FILENAME);
    fputs("\n  | Environment Variables |\n", stderr);
    fprintf(stderr,
            "    %-20s\t%s\n",
            FRAGMENT_SIZE,
            "approximate maximum size of a single file fragment");
    fprintf(
      stderr,
      "    %-20s\t%s\n",
      STDIN_NO_DATA_TIMEOUT,
      "number of seconds to wait for stdin to have data before giving up");
    retcode = 2;
    goto done;
  }

  if (fragment_size < BUFSZ) {
    fprintf(stderr,
            "file fragment size '%zu' cannot be less than internal buffer; "
            "increasing size to '%d'\n",
            fragment_size,
            BUFSZ);
    fragment_size = BUFSZ;
  }

  // If the STDIN file descriptor is not ready, the program was probably
  // executed without redirecting output from another program.
  fd_set readset;
  FD_ZERO(&readset);
  FD_SET(STDIN_FILENO, &readset);
  struct timeval timeout = { .tv_sec = stdin_no_data_timeout,
                             .tv_usec = 500000 };
  if (select(1, &readset, NULL, NULL, &timeout) < 1) {
    fprintf(stderr, "did you forget to pipe in another program's stdout?\n");
    retcode = 1;
    goto done;
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
