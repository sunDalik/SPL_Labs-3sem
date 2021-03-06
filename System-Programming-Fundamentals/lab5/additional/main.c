#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <getopt.h>
#include <pthread.h>
#include <math.h>
#include <fcntl.h>
#include <signal.h>
#include "message_format.h"

#define PER_THREAD 1
#define PER_TASK 2
#define THREAD_POOL 3

int thread_count = 3;
int strategy = PER_THREAD;
pthread_mutex_t mutex;
pthread_t reader_thread, writer_thread, fib_task_thread, pow_task_thread, bubble_task_thread;
int writer_pipe[2];
int fib_task_pipe[2];
int pow_task_pipe[2];
int bubble_task_pipe[2];
FILE *result_log_file;
TMessage empty_message;

bool read_all(int fd, void *buf, size_t bytes) {
    size_t bytes_read = 0;
    while (bytes_read < bytes) {
        ssize_t curr_read = read(fd, (char *) buf + bytes_read, bytes - bytes_read);
        //EOF
        if (curr_read <= 0)
            return false;
        bytes_read += curr_read;
    }
    return true;
}

void close_pipe(int pipe_fd[]) {
    close(pipe_fd[0]);
    close(pipe_fd[1]);
}

void goodbye() {
    printf("\nGoodbye!\n");
    pthread_cancel(reader_thread);
    pthread_cancel(writer_thread);
    pthread_cancel(fib_task_thread);
    pthread_cancel(pow_task_thread);
    pthread_cancel(bubble_task_thread);
    close_pipe(writer_pipe);
    close_pipe(fib_task_pipe);
    close_pipe(pow_task_pipe);
    close_pipe(bubble_task_pipe);
    close(fileno(result_log_file));
    exit(EXIT_SUCCESS);
}

void lock() {
    pthread_mutex_lock(&mutex);
}

void unlock() {
    pthread_mutex_unlock(&mutex);
}

uint32_t fibonacci(uint32_t n) {
    if (n <= 1)
        return 1;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

void bubble_sort(uint32_t arr[], int n) {
    int i, j;
    for (i = 0; i < n - 1; i++) {
        for (j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                uint32_t temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

void write_tmessage(int fd, TMessage message) {
    write(fd, &message.Type, sizeof(message.Type));
    write(fd, &message.Size, sizeof(message.Size));
    write(fd, message.Data, message.Size);
}

TMessage read_to_tmessage(int fd) {
    TMessage *message = malloc(sizeof(TMessage));
    if (read_all(fd, &message->Type, sizeof(message->Type)) == false) return empty_message;
    if (read_all(fd, &message->Size, sizeof(message->Size)) == false) return empty_message;
    message->Data = calloc(message->Size, 1);
    if (read_all(fd, message->Data, message->Size) == false) return empty_message;
    return *message;
}

TMessage read_tmessage(int fd) {
    TMessage *message = malloc(sizeof(TMessage));
    if (read_all(fd, &message->Type, sizeof(message->Type)) == false) return empty_message;
    if (read_all(fd, &message->Size, sizeof(message->Size)) == false) return empty_message;
    if (read_all(fd, &message->Data, sizeof(message->Data)) == false) return empty_message;
    return *message;
}

void *writer() {
    while (1) {
        TMessage msg = read_tmessage(writer_pipe[0]);
        if (msg.Type != NONE) {
            printf("WRITER: Write to file: data %d, type %d\n", *msg.Data, msg.Type);
            write_tmessage(fileno(result_log_file), msg);
        }
    }
}

TMessage *make_out_fib_message(TMessage msg) {
    TMessage *out_msg = malloc(sizeof(TMessage));
    out_msg->Type = FIBONACCI;
    uint32_t result = fibonacci(*msg.Data);
    out_msg->Size = 4;
    out_msg->Data = &result;
    return out_msg;
}

TMessage *make_out_pow_message(TMessage msg) {
    TMessage *out_msg = malloc(sizeof(TMessage));
    out_msg->Type = POW;
    uint32_t result = pow(msg.Data[0], msg.Data[1]);
    out_msg->Size = 4;
    out_msg->Data = &result;
    return out_msg;
}

TMessage *make_out_bubble_message(TMessage msg) {
    TMessage *out_msg = malloc(sizeof(TMessage));
    out_msg->Type = BUBBLE_SORT;
    bubble_sort(msg.Data, msg.Size / 4);
    out_msg->Size = msg.Size;
    out_msg->Data = msg.Data;
    return out_msg;
}

void *handle_tmessage(void *args) {
    TMessage msg = *((TMessage *) args);
    TMessage *out_msg = malloc(sizeof(TMessage));
    switch (msg.Type) {
        case FIBONACCI:
            out_msg = make_out_fib_message(msg);
            break;
        case POW:
            out_msg = make_out_pow_message(msg);
            break;
        case BUBBLE_SORT:
            out_msg = make_out_bubble_message(msg);
            break;
        case NONE:
            return 0;
    }
    printf("THREAD ID %ld: Send to writer thread: data %d, type %d\n", pthread_self(), *out_msg->Data, out_msg->Type);
    write(writer_pipe[1], out_msg, sizeof(TMessage));
    return 0;
}

void *handle_fib() {
    while (1) {
        TMessage msg = read_tmessage(fib_task_pipe[0]);
        if (msg.Type == FIBONACCI) {
            TMessage *out_msg = make_out_fib_message(msg);
            printf("THREAD ID %ld: Send to writer thread: data %d, type %d\n", pthread_self(), *out_msg->Data,
                   out_msg->Type);
            write(writer_pipe[1], out_msg, sizeof(TMessage));
        }
    }
}

void *handle_pow() {
    while (1) {
        TMessage msg = read_tmessage(pow_task_pipe[0]);
        if (msg.Type == POW) {
            TMessage *out_msg = make_out_pow_message(msg);
            printf("THREAD ID %ld: Send to writer thread: data %d, type %d\n", pthread_self(), *out_msg->Data,
                   out_msg->Type);
            write(writer_pipe[1], out_msg, sizeof(TMessage));
        }
    }
}

void *handle_bubble() {
    while (1) {
        TMessage msg = read_tmessage(bubble_task_pipe[0]);
        if (msg.Type == BUBBLE_SORT) {
            TMessage *out_msg = make_out_bubble_message(msg);
            printf("THREAD ID %ld: Send to writer thread: data %d, type %d\n", pthread_self(), *out_msg->Data,
                   out_msg->Type);
            write(writer_pipe[1], out_msg, sizeof(TMessage));
        }
    }
}

void *reader() {
    while (1) {
        TMessage msg = read_to_tmessage(STDIN_FILENO);
        if (msg.Type == NONE) return 0;
        pthread_t new_thread;
        char *name = "";

        switch (strategy) {
            case PER_THREAD:
                printf("READER: Create new thread for the task of type %d\n", msg.Type);
                pthread_create(&new_thread, NULL, handle_tmessage, (void *) &msg);
                break;
            case PER_TASK:
                switch (msg.Type) {
                    case FIBONACCI:
                        name = "FIBONACCI";
                        write(fib_task_pipe[1], &msg, sizeof(TMessage));
                        break;
                    case POW:
                        name = "POW";
                        write(pow_task_pipe[1], &msg, sizeof(TMessage));
                        break;
                    case BUBBLE_SORT:
                        name = "BUBBLE";
                        write(bubble_task_pipe[1], &msg, sizeof(TMessage));
                        break;
                    case NONE:
                        return 0;
                }
                printf("READER: Send task to %s thread: data %d, type %d\n", name, *msg.Data, msg.Type);
                break;
            case THREAD_POOL:
                //
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    empty_message.Type = NONE;
    struct sigaction action = {.sa_handler = goodbye, .sa_flags = 0};
    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGKILL, &action, NULL);

    static struct option long_options[] = {{"thread_count", required_argument, 0, 't'},
                                           {"strategy",     required_argument, 0, 's'},
                                           {NULL, 0, NULL,                        0}};
    int opt = 0;
    while ((opt = getopt_long(argc, argv, "t:s:", long_options, NULL)) != -1) {
        switch (opt) {
            case 't':
                thread_count = (int) strtol(optarg, NULL, 10);
                break;
            case 's':
                if (strncmp(optarg, "per_thread", 10) == 0) {
                    strategy = PER_THREAD;
                } else if (strncmp(optarg, "per_task", 8) == 0) {
                    strategy = PER_TASK;
                } else if (strncmp(optarg, "thread_pool", 11) == 0) {
                    strategy = THREAD_POOL;
                }
                break;
        }
    }

    pthread_mutex_init(&mutex, NULL);

    pipe(writer_pipe);
    pipe(fib_task_pipe);
    pipe(pow_task_pipe);
    pipe(bubble_task_pipe);

    result_log_file = fopen("result_log", "w");

    if (strategy == PER_TASK) {
        pthread_create(&fib_task_thread, NULL, handle_fib, NULL);
        pthread_create(&pow_task_thread, NULL, handle_pow, NULL);
        pthread_create(&bubble_task_thread, NULL, handle_bubble, NULL);
    }

    pthread_create(&reader_thread, NULL, reader, NULL);
    pthread_create(&writer_thread, NULL, writer, NULL);
    pthread_join(reader_thread, NULL);
    pthread_join(writer_thread, NULL);
    return EXIT_SUCCESS;
}