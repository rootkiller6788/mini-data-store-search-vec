#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(void) {
    for (int i = 0; i < 10; i++) {
        void *p = malloc(1024);
        printf("ptr %d: %p, as int32=%08x, roundtrip=%p\n", i, p, (int32_t)(intptr_t)p, (void*)(intptr_t)(int32_t)(intptr_t)p);
    }
    return 0;
}
