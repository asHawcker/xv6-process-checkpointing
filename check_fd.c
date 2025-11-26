#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]) {
  int fd;
  int n;
  
  printf(1, "\n=== FD CHECKPOINT TEST ===\n");

  printf(1, "Opening 'test_log.txt'...\n");
  fd = open("test_log.txt", O_CREATE | O_RDWR);
  
  if(fd < 0){
      printf(1, "Error: Create failed.\n");
      exit();
  }
  printf(1, "File Descriptor %d assigned.\n", fd);

  printf(1, "Writing 'BEFORE' to file...\n");
  if(write(fd, "BEFORE CHECKPOINT\n", 18) != 18){
      printf(1, "Error: Write failed.\n");
      close(fd);
      exit();
  }

  printf(1, "Checkpointing to 'chk_fd.img'...\n");
  if(checkpoint(getpid(), "chk_fd.img") < 0){
      printf(1, "Checkpoint failed!\n");
      exit();
  }
  
  printf(1, "Resumed. Attempting to write 'AFTER'...\n");

  n = write(fd, "AFTER CHECKPOINT\n", 17);
  
  if(n < 0){
      printf(1, "\n>>> Write FAILED\n");
      printf(1, "Reason: this is a RESTORED process.\n");
      printf(1, "'fd' is closed by the kernel.\n");
  } else {
      printf(1, "\n>>> Write SUCCESS\n");
  }

  close(fd);
  exit();
}