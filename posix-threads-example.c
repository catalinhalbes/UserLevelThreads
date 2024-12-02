#include <stdio.h>
#include <pthread.h>
#include <stdint.h>

typedef struct {
    char character;
    uint64_t counter;
}thread_arg;

void* print_char(void* arg) {
    thread_arg targ = *((thread_arg*) arg); // this makes a copy... oh well
    uint64_t c = 0;

    while(1) {
        if (c == 0)
            printf("%c\n", targ.character);
        
        c = (c + 1) % targ.counter;
    }
    return NULL;
}

int main() {
    pthread_t threads[3];
    thread_arg args[3] = {{'A', 100000000ull}, {'B', 200000000ull}, {'C', 400000000ull}};

    for (int i = 0; i < 3; i++)
        pthread_create(&threads[i], NULL, print_char, args + i);

    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], NULL);

    return 0;
}
