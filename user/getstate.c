#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
    int pid = getpid();
    char* state = malloc(16);
    if(getprocstate(pid, state, 10) == -1) {
        printf(1, "FAILURE");
        exit();
    }
    printf(1, "\nPROCSTATE: %s\n", state);
    exit();
}
