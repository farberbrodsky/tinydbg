#include <stdio.h>
#include <unistd.h>
int main() {
    for (int i = 0; i < 5; i++) {
        // asm("int $3");
        printf("hello\n");
        if (i != 4) {
            sleep(2);
        }
    }
    return 3;
}
