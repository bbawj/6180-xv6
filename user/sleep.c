#include "kernel/types.h"

#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(2, "No ticks argument provided to sleep\n");
    exit(0);
  }

  int ticks = atoi(argv[1]);

  sleep(ticks);
  exit(0);
}
