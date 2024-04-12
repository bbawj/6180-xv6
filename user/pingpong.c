#include "kernel/types.h"

#include "kernel/stat.h"
#include "user/user.h"

int p[2];

int main(int argc, char *argv[]) {
  char byte = 1;
  char buf;
  pipe(p);
  int id = fork();
  if (id == 0) {
    read(p[0], &buf, 1);
    printf("%d: received ping\n", getpid());
    write(p[1], &byte, 1);
    close(p[1]);
  } else {
    write(p[1], &byte, 1);
    read(p[0], &buf, 1);
    printf("%d: received pong\n", getpid());
    close(p[0]);
  }

  exit(0);
}
