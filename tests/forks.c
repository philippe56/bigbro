#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
  int num;
  if (argc < 2) {
    num = 0;
  } else {
    sscanf(argv[1], "%d", &num);
  }
  if (num == 7) {
    if (chdir("tmp")) exit(1);
    argv[0] = "../tests/forks.test";
  }
  if (num == 37) {
    if (chdir("subdir2")) exit(1);
    argv[0] = "../../tests/forks.test";
  }
  if (num == 73) {
    if (chdir("..")) exit(1);
    argv[0] = "../tests/forks.test";
  }
  if (num == 137) {
    if (chdir("subdir1")) exit(1);
    argv[0] = "../../tests/forks.test";
  }
  if (num > 1000) {
    printf("We are finally all done!\n");
    if (!fopen("deepdir/forks", "w")) exit(1);
    exit(0);
  }
  pid_t child = fork();
  if (child) {
    printf("Child %d was %d\n", num, child);
    waitpid(child, 0, 0);
    exit(0);
  } else {
    char *numname = malloc(1000);
    sprintf(numname, "%d", num+1);
    execl(argv[0], argv[0], numname, (char *)0);
    printf("execl failed: %s\n", strerror(errno));
  }
  return 0;
}
