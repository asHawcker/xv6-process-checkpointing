#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
  int pid = fork();

  if(pid == 0){
      // CHILD CODE
      int count = 0;
      while(1){
          if(count % 5 == 0) 
              printf(1, "Child (PID %d) is alive. Count: %d\n", getpid(), count);
          count++;
          sleep(5);
      }
  } else {
      printf(1, "Parent: I created child PID %d.\n", pid);
      sleep(100); // let child run for some time and then we checkpoint and kill

      printf(1, "Parent: Checkpointing child PID %d to 'child_dump.img'...\n", pid);
      if(checkpoint(pid, "child_dump.img") == 0){
          printf(1, "Parent: Checkpoint Success.\n");
      } else {
          printf(1, "Parent: Checkpoint Failed.\n");
      }

      printf(1, "Parent: Killing child now.\n");
      kill(pid);
      wait();
      exit();
  }
}