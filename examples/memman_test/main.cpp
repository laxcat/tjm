#include "MemMan.h"
#include "memory.h"
#include <stdio.h>

int main(int argc, char ** argv) {

    mem.printStatus();

    void * ptr0 = mem.alloc(15, "fart0");
    // void * ptr1 = mem.alloc(10, "fart1");
    // void * ptr2 = mem.alloc(10, "fart2");
    // void * ptr3 = mem.alloc(10, "fart3");

    // printf("ptr0: %p\n", ptr0);
    // printf("ptr1: %p\n", ptr1);
    // printf("ptr2: %p\n", ptr2);
    // printf("ptr3: %p\n", ptr3);

    mem.printStatus();

    mem.free(ptr0);

    mem.printStatus();

    // ptr0 = mem.alloc(15, "fart0");

    // printf("ptr0: %p\n", ptr0);
    // printf("ptr1: %p\n", ptr1);
    // printf("ptr2: %p\n", ptr2);
    // printf("ptr3: %p\n", ptr3);

    // mem.printStatus();
    return 0;
}
