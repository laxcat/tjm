#define MEMMAN_ENABLE_PRINT
#include "MemMan.h"
#undef MEMMAN_ENABLE_PRINT
#include "memory.h"

int main(int argc, char ** argv) {

    mem.printStatus("before alloc");

    void * ptr0 = mem.alloc(16, "fart0");
    void * ptr1 = mem.alloc(17, "fart1");
    void * ptr2 = mem.alloc(17, "fart2");
    void * ptr3 = mem.alloc(17, "fart3");

    // printf("ptr0: %p\n", ptr0);
    // printf("ptr1: %p\n", ptr1);
    // printf("ptr2: %p\n", ptr2);
    // printf("ptr3: %p\n", ptr3);

    mem.printStatus("after alloc");

    mem.free(ptr0);
    mem.free(ptr1);
    mem.free(ptr2);
    mem.free(ptr3);

    mem.printStatus("after free");

    // ptr0 = mem.alloc(15, "fart0");

    // printf("ptr0: %p\n", ptr0);
    // printf("ptr1: %p\n", ptr1);
    // printf("ptr2: %p\n", ptr2);
    // printf("ptr3: %p\n", ptr3);

    // mem.printStatus();
    return 0;
}
