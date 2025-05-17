#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BUF_SZ 64

static void send_line(int fd, const char *s)
{
    write(fd, s, strlen(s));
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <days>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int days = atoi(argv[3]);
    if (days <= 0) { fprintf(stderr, "Days must be >0\n"); return 1; }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in srv = { .sin_family = AF_INET,
                               .sin_port   = htons(atoi(argv[2])) };
    inet_pton(AF_INET, argv[1], &srv.sin_addr);
    if (connect(fd, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("connect"); return 1;
    }

    char req[BUF_SZ];
    snprintf(req, sizeof(req), "DAYS=%d\n", days);
    send_line(fd, req);

    char buf[BUF_SZ];
    while (1) {
        int n = read(fd, buf, sizeof(buf)-1);
        if (n <= 0) { puts("Connection closed"); break; }
        buf[n] = '\0';

        if (strncmp(buf, "ASSIGNED", 8) == 0) {
            int room; sscanf(buf, "ASSIGNED %d", &room);
            printf("[client] Got room %d\n", room);
        } else if (strncmp(buf, "WAIT", 4) == 0) {
            puts("[client] No rooms — waiting on the bench…");
        } else if (strncmp(buf, "CHECKOUT", 8) == 0) {
            puts("[client] Stay finished — checking out!");
            break;
        } else {
            printf("[client] Unknown message: %s", buf);
        }
    }
    close(fd);
    return 0;
}
