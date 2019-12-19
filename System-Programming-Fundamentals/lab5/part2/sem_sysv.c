#include "alphabet.h"
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>


int sem_id;

void lock() {
    struct sembuf operations[2] = {
            {.sem_op = 0},
            {.sem_op = 1}
    };
    semop(sem_id, operations, 2);
}

void unlock() {
    struct sembuf operations = {.sem_op = -1};
    semop(sem_id, &operations, 1);
}

void *inv_f(void *args) {
    int *interval = (int *) args;
    while (1) {
        usleep(*interval);
        lock();
        invert_case();
        unlock();
    }
}

void *swp_f(void *args) {
    int *interval = (int *) args;
    while (1) {
        usleep(*interval);
        lock();
        swap_alphabet();
        unlock();
    }
}

bool parse_int(const char *str, int *var) {
    char *end;
    *var = (int) strtol(str, &end, 10);
    if (end == str) return false;
    return true;
}

int main(int argc, char *argv[]) {
    // thread descriptors init
    pthread_t inv_thread, swp_thread;
    // setting up intervals
    int inv_interval = 100, swp_interval = 200, main_interval = 1000, cnt_interval = 10;
    int opt = 0;
    while ((opt = getopt(argc, argv, "i:s:m:c:")) != -1) {
        switch (opt) {
            case 'i':
                if (!parse_int(optarg, &inv_interval)) {
                    printf("-i parameter must be integer!");
                    return 1;
                }
                break;
            case 's':
                if (!parse_int(optarg, &swp_interval)) {
                    printf("-s parameter must be integer!");
                    return 1;
                }
                break;
            case 'm':
                if (!parse_int(optarg, &main_interval)) {
                    printf("-m parameter must be integer!");
                    return 1;
                }
                break;
            case 'c':
                if (!parse_int(optarg, &cnt_interval)) {
                    printf("-c parameter must be integer!");
                    return 1;
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-i] [-s] [-m] [-c]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    sem_id = semget(IPC_PRIVATE, 2, 0600 | IPC_CREAT);
    semctl(sem_id, 0, SETALL, 1);

    // THREADS INIT
    pthread_create(&inv_thread, NULL, inv_f, (void *) &inv_interval);
    pthread_create(&swp_thread, NULL, swp_f, (void *) &swp_interval);

    while (1) {
        usleep(main_interval);
        lock();
        print_alphabet();
        unlock();
    }
}