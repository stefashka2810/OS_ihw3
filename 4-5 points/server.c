#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_ROOMS 10
#define BACKLOG   20
#define BUF_SZ    64

static int DAY_SEC = 5;

typedef struct {
    int  occupied;          
    int  days_left;               
    int  client_fd;                    
} Room;

typedef struct WaitNode {
    int client_fd;
    int days;
    struct WaitNode *next;
} WaitNode;

static Room rooms[MAX_ROOMS];
static WaitNode *bench_head = NULL, *bench_tail = NULL;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static void send_line(int fd, const char *line)
{
    write(fd, line, strlen(line));
}

static void enqueue(int fd, int days)
{
    WaitNode *n = malloc(sizeof(*n));
    n->client_fd = fd;
    n->days = days;
    n->next = NULL;
    if (!bench_tail) bench_head = bench_tail = n;
    else { bench_tail->next = n; bench_tail = n; }
}

static WaitNode *dequeue(void)
{
    if (!bench_head) return NULL;
    WaitNode *n = bench_head;
    bench_head   = bench_head->next;
    if (!bench_head) bench_tail = NULL;
    return n;
}

typedef struct {
    int room_id;
    int days;
} RoomArgs;

static void *room_thread(void *arg)
{
    RoomArgs *ra = arg;
    sleep(ra->days * DAY_SEC);

    pthread_mutex_lock(&mtx);
    int fd = rooms[ra->room_id].client_fd;
    send_line(fd, "CHECKOUT\n");
    close(fd);

    rooms[ra->room_id].occupied = 0;
    rooms[ra->room_id].client_fd = -1;
    printf("Room %d became free\n", ra->room_id);

    WaitNode *w = dequeue();
    if (w) {
        rooms[ra->room_id].occupied   = 1;
        rooms[ra->room_id].days_left  = w->days;
        rooms[ra->room_id].client_fd  = w->client_fd;
        char msg[BUF_SZ];
        snprintf(msg, sizeof(msg), "ASSIGNED %d\n", ra->room_id);
        send_line(w->client_fd, msg);
        printf("Queued guest got room %d for %d day(s). Queue len now %zu\n",
            ra->room_id, w->days,
            ({ size_t n=0; for (WaitNode *t=bench_head;t;t=t->next) ++n; n; }));
        RoomArgs *newra = malloc(sizeof(*newra));
        *newra = (RoomArgs){ .room_id = ra->room_id, .days = w->days };
        pthread_t tid; pthread_create(&tid, NULL, room_thread, newra);
        pthread_detach(tid);
        free(w);
    }
    pthread_mutex_unlock(&mtx);
    free(ra);
    return NULL;
}

static void *client_thread(void *arg)
{
    int fd = *(int*)arg;
    free(arg);

    char buf[BUF_SZ] = {0};
    int n = read(fd, buf, sizeof(buf)-1);
    if (n <= 0) { close(fd); return NULL; }

    int days = 0;
    if (sscanf(buf, "DAYS=%d", &days) != 1 || days <= 0) {
        send_line(fd, "ERROR\n"); close(fd); return NULL;
    }

    pthread_mutex_lock(&mtx);
    int room_id = -1;
    for (int i = 0; i < MAX_ROOMS; ++i)
        if (!rooms[i].occupied) { room_id = i; break; }

    if (room_id >= 0) {                       
        rooms[room_id].occupied  = 1;
        rooms[room_id].days_left = days;
        rooms[room_id].client_fd = fd;

        char msg[BUF_SZ];
        snprintf(msg, sizeof(msg), "ASSIGNED %d\n", room_id);
        send_line(fd, msg);
        printf("Guest assigned room %d for %d day(s)\n", room_id, days);

        RoomArgs *ra = malloc(sizeof(*ra));
        *ra = (RoomArgs){ .room_id = room_id, .days = days };
        pthread_t tid; pthread_create(&tid, NULL, room_thread, ra);
        pthread_detach(tid);
    } else {                                  
        enqueue(fd, days);
        size_t qlen = 0; for (WaitNode *t = bench_head; t; t = t->next) ++qlen;
        send_line(fd, "WAIT\n");
        printf("Guest queued (%zu ahead)\n", qlen - 1);
    }
    pthread_mutex_unlock(&mtx);

    return NULL;
}


int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <bind_ip> <port> [DAY_SEC]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 4) DAY_SEC = atoi(argv[3]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_port   = htons(atoi(argv[2])) };
    inet_pton(AF_INET, argv[1], &addr.sin_addr);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(listen_fd, BACKLOG) < 0) {
        perror("bind/listen"); return EXIT_FAILURE;
    }

    printf("Hotel server on %s:%s  (1 day = %d sec)\n", argv[1], argv[2], DAY_SEC);

    while (1) {
        int *fd = malloc(sizeof(int));
        *fd = accept(listen_fd, NULL, NULL);
        if (*fd < 0) { perror("accept"); free(fd); continue; }

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, fd);
        pthread_detach(tid);
    }
}
