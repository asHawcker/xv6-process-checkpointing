#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"


struct check_point_header {
  uint id;
  int pid;
  uint sz;
  char name[16];
  uchar tf[76];
};

int main(int argc, char *argv[]) {
  if(argc < 2){
    printf(1, "Usage: imginfo <checkpoint_img>\n");
    exit();
  }

  int fd = open(argv[1], O_RDONLY);
  if(fd < 0){
    printf(1, "Error: Could not open file %s\n", argv[1]);
    exit();
  }

  struct check_point_header hdr;
  if(read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)){
    printf(1, "Error: File too small \n");
    close(fd);
    exit();
  }

  printf(1, "------- Checkpoint Image Info -------\n");
  printf(1, "File: %s\n", argv[1]);
  
  if(hdr.id != 0xDEADBEEF){
      printf(1, "Status: INVALID image file");
  } else {
      printf(1, "Status: VALID XV6 checkpoint image file\n");
      printf(1, "Original Name: %s\n", hdr.name);
      printf(1, "Orignal PID:  %d\n", hdr.pid);
      printf(1, "Memory Size:  %d bytes (%d KB)\n", hdr.sz, hdr.sz/1024);
  }
  printf(1, "----------------------------------\n");

  close(fd);
  exit();
}