
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

int SharedVariable = 0;
pthread_barrier_t barrier;
pthread_mutex_t lock;

void SimpleThread(int which);

void *myThread(void *i);

void *myThread(void *i) {
    SimpleThread(*((int *) i));
}

void SimpleThread(int which) {
    int num, val;
    pthread_mutex_lock(&lock);
    for (num = 0; num < 20; num++) {
        if (random() > RAND_MAX / 2)
            usleep(500);
        val = SharedVariable;
        printf("*** thread %d sees value %d\n", which, val);
        SharedVariable = val + 1;
    }
    pthread_mutex_unlock(&lock);
    pthread_barrier_wait(&barrier);
    val = SharedVariable;
    printf("Thread %d sees final value %d\n", which, val);
}

int main(int argc, char *argv[]) {

    long numbOfThreads = 0;
    int i = 0;

    if (argc != 2) {
        printf("Usage: threading <number of threads>\n");
        exit(1);
    }

    numbOfThreads = strtol(argv[1], NULL, 10);

    pthread_t tid[numbOfThreads];

    if (pthread_barrier_init(&barrier, NULL, (uint) numbOfThreads) != 0) {
        printf("\n Barrier Init Failed\n");
        exit(1);
    }
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("\n Mutex Init Failed\n");
        exit(1);
    }

    int threadNumber[numbOfThreads];
    for (; i < numbOfThreads; i++) {
        threadNumber[i] = i;
        pthread_create(&tid[i], NULL, myThread, &threadNumber[i]);
    }

    for (i = 0; i < numbOfThreads; i++) {
        pthread_join(tid[i], NULL);
    }
    pthread_barrier_destroy(&barrier);
    pthread_mutex_destroy(&lock);
    return 0;
}