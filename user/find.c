// Simple grep.  Only supports ^ . * $ operators.
#include "kernel/types.h"

#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

char buf[1024];
int match(char *, char *);

void find(char *path, char *pattern) {
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if (st.type != T_DIR) {
    fprintf(2, "find: path is not a directory\n");
    return;
  }

  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
    printf("find: path too long\n");
    return;
  }

  char buf[512];
  strcpy(buf, path);
  char *p = buf + strlen(buf);
  *p++ = '/';
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    if (stat(buf, &st) < 0) {
      printf("find: cannot stat %s\n", buf);
      continue;
    }
    if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {
      char nested[512];
      strcpy(nested, buf);
      find(nested, pattern);
    } else {
      if (strcmp(p, pattern) == 0)
        printf("%s\n", buf);
    }
  }
  close(fd);
}

int main(int argc, char *argv[]) {
  char *path;
  char *pattern;

  if (argc <= 2) {
    fprintf(2, "usage: find dir pattern\n");
    exit(0);
  }

  path = argv[1];
  pattern = argv[2];

  find(path, pattern);
  exit(0);
}
