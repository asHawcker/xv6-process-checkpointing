#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
  if(argc < 2){
    printf(1, "Usage: check <filename>\n");
    exit();
  }

  int pid = getpid();
  printf(1, "Checkpointing process %d to file %s...\n", pid, argv[1]);

  if(checkpoint(pid, argv[1]) < 0){
      printf(1, "Checkpoint failed\n");
  } else {
      printf(1, "Checkpoint successful! I am saved.\n");
  }
  
  int i = 0;
  while(i<100){
      printf(1, "Counter: %d\n", i++);
      checkpoint(getpid(), argv[1]);
      sleep(5);
  }
  exit();
}