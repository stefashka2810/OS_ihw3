#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define STAMP 32
#define BACKLOG 8

typedef struct LogPeer {
    int fd;
    struct LogPeer *next;
} LogPeer;

static struct {
    int             listen_fd;
    pthread_t       tid;
    LogPeer        *peers;
    pthread_mutex_t mtx;
} L;

static void ts(char *b, size_t n)
{
    time_t t = time(NULL);
    strftime(b, n, "%F %T", localtime(&t));
}

static void *loop(void *arg)
{
    (void)arg;
    while (1) {
        int fd = accept(L.listen_fd, NULL, NULL);
        if (fd < 0) continue;

        pthread_mutex_lock(&L.mtx);
        LogPeer *p = malloc(sizeof *p);
        p->fd = fd; p->next = L.peers; L.peers = p;
        pthread_mutex_unlock(&L.mtx);

        dprintf(fd, "# connected to hotel logger\n");
    }
}

int  logger_init(const char *ip, int port)
{
    pthread_mutex_init(&L.mtx, NULL);
    L.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(L.listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
    inet_pton(AF_INET, ip, &a.sin_addr);
    if (bind(L.listen_fd, (void*)&a, sizeof a) || listen(L.listen_fd, BACKLOG))
        return -1;

    return pthread_create(&L.tid, NULL, loop, NULL);
}

void logger_log(const char *fmt, ...)
{
    char stamp[STAMP], line[256], out[300];
    ts(stamp, sizeof stamp);

    va_list ap; va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    printf("%s  %s\n", stamp, line);
    fflush(stdout);

    snprintf(out, sizeof out, "%s  %s\n", stamp, line);

    pthread_mutex_lock(&L.mtx);
    LogPeer *prev = NULL, *p = L.peers;
    while (p) {
        if (write(p->fd, out, strlen(out)) < 0) {
            if (prev) prev->next = p->next; else L.peers = p->next;
            close(p->fd); LogPeer *tmp = p; p = p->next; free(tmp); continue;
        }
        prev = p; p = p->next;
    }
    pthread_mutex_unlock(&L.mtx);
}
