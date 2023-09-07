#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/queue.h>
#include <pthread.h>
#include <time.h>
#include "time_functions_shared.h"

#define PORT "9000"
#define BACKLOG 20
#define SOCKET_DATA "/var/tmp/aesdsocketdata"
#define BUF_LEN 1024

bool end_signal = false;

struct thread_data {
    int fd;
    bool complete;
};
struct timer_data {
    pthread_mutex_t lock;
};
typedef struct slist_data_s slist_data_t;
struct slist_data_s  {
    pthread_t thread_id;
    struct thread_struct th_data;
    SLIST_ENTRY(slist_data_s) entries;
};

SLIST_HEAD(slisthead, slist_data_s) head = SLIST_HEAD_INITIALIZER(head);
pthread_mutex_t lock;

//runs every 10 seconds
static void timer_thread(union sigval sigval) signal_handler