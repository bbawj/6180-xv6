// Simple grep.  Only supports ^ . * $ operators.
#include "kernel/types.h"

#include "kernel/fs.h"
#include "kernel/param.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  if (argc <= 2) {
    fprintf(2, "xargs: xargs <command> <args>\n");
    exit(0);
  }

  int arg_count = 0;
  char *args[MAXARG] = {0};

  const char *command = argv[1];
  args[arg_count++] = argv[1];

  for (int i = 2; i < argc; ++i) {
    args[arg_count++] = argv[i];
  }

  char buf[1024];
  char *p = buf;
  int total = 0;

  for (;;) {
    char temp;
    int ret = read(0, &temp, 1);
    if (ret <= 0) {
      break;
    }
    ++total;
    if (temp == '\n') {
      args[arg_count] = malloc(total);
      memcpy(args[arg_count], buf, total);
      p = buf;
      total = 0;

      int pid = fork();
      if (pid == 0) {
        // for (int i = 0; i < arg_count; ++i) {
        //   printf("%s\n", args[i]);
        // }
        exec(command, args);
      }
      wait(0);
    } else {
      *p++ = temp;
    }
  }

  exit(0);
}
