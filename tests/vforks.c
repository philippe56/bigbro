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
  if (num > 1000) {
    printf("We are finally all done!\n");
    if (!fopen("tmp.vforks", "w")) exit(1);
    exit(0);
  }
  pid_t child = vfork();
  if (child) {
    printf("Child %d was %d\n", num, child);
    int status;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
      printf("Child did not exit right.\n");
      exit(1);
    }
    exit(0);
  } else {
    char *numname = malloc(1000);
    sprintf(numname, "%d", num+1);
    execl(argv[0], argv[0], numname, (char *)0);
    printf("execl failed: %s\n", strerror(errno));
  }
  return 0;
}
