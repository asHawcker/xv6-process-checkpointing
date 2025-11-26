#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
  if(argc < 2){
    printf(1, "Usage: recover <checkpoint_file>\n");
    exit();
  }
  
  printf(1, "Restoring from %s...\n", argv[1]);
  if(restart(argv[1]) < 0){
    printf(1, "Restart failed\n");
  }
  
  exit();
}