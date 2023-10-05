// Do not modify this file. It will be replaced by the grading scripts
// when checking your project.

#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    int* p = (int*)sbrk(8*1024*1024);
    // allocate memory that would cause a huge page to be allocated
    *p = 1;
    // Set the value to 1. Shouldn't cause a page fault
    *(p + 1024 * 1024 + 10)  = 1;
    exit();
}
