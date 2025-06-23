#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main() {
    unsigned char *realPtr = calloc(50000, 1);
    assert(realPtr != NULL);
    unsigned char *ptr = realPtr;
    ptr += 3;
    *ptr = 0;
    *ptr = 0;
    *ptr += 65;
    ptr += 3;
    *ptr = 0;
    ptr += 4;
    *ptr = 0;
    ptr -= 7;
    unsigned char v = *ptr;
    *(ptr + 7) += v;
    *(ptr + 3) += v;
    *ptr = 0;
    ptr += 3;
    v = *ptr;
    *(ptr + -3) += v;
    *ptr = 0;
    ptr += 4;
    putchar(*ptr);
    ptr -= 7;
    *ptr = 0;
    ptr += 7;
    ++(*ptr);
    putchar(*ptr);
    ptr -= 7;
    *ptr = 0;
    ptr += 7;
    --(*ptr);
    putchar(*ptr);
    free(realPtr);
    return 0;
}
