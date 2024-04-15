#include "kernel/types.h"

#include "kernel/stat.h"
#include "user/user.h"

static int read_all(int *val, int in) {
  int total = 0;
  while (total != sizeof(int)) {
    int rc = read(in, val + total, sizeof(int) - total);
    if (rc <= 0) {
      return -1;
    }
    total += rc;
  }
  return 0;
}

static int write_all(int val, int out) {
  int written = 0;
  while (written != sizeof(int)) {
    int rc = write(out, &val + written, sizeof(int) - written);
    if (rc <= 0) {
      return -1;
    }
    written += rc;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  int prime = -1;
  int buf = 0;
  int pid = 0;
  int in[2];
  int out[2] = {-1, -1};
  pipe(in);
  pid = fork();
  if (pid == 0) {
    close(in[1]);
    read(in[0], &buf, sizeof(int));
    printf("prime %d\n", buf);
    if (prime == -1) prime = buf;

    for (;;) {
      int ret = read_all(&buf, in[0]);
      if (ret == -1) {
        close(in[0]);
        close(out[1]);
        break;
      }
      if (buf % prime != 0) {
        if (out[1] == -1) {
          if (pipe(out) == -1) {
            // printf("prime %d\n", buf);
            continue;
          }
          pid = fork();
          if (pid == 0) {
            prime = buf;
            printf("prime %d\n", prime);
            in[0] = out[0];
            in[1] = out[1];
            // We need to close our new reference to unused write-end pipe that
            // parent uses to communicate with us
            close(in[1]);
            out[0] = -1;
            out[1] = -1;
          } else if (pid > 0) {
            // Close our reference to unused read-end of output
            close(out[0]);
          }
          continue;
        }
        if (write_all(buf, out[1]) == -1) {
          close(in[0]);
          close(out[1]);
          break;
        }
      }
    }
  } else {
    close(in[0]);
    for (int i = 2; i <= 35; ++i) {
      write(in[1], &i, sizeof(int));
    }
    close(in[1]);
  }

  if (pid > 0) {
    wait(0);
  }

  exit(0);
}
