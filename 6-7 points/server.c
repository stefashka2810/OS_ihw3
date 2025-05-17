int  logger_init(const char *ip, int port);
void logger_log(const char *fmt, ...);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LOG_PORT  6000
#define MAX_ROOMS 10
#define BACKLOG   32
#define BUF       64

static int DAY_SEC = 5;

typedef struct { int busy, days, fd; } Room;
typedef struct Wait { int fd, days; struct Wait *next; } Wait;

static Room rooms[MAX_ROOMS];
static Wait *q_head = NULL, *q_tail = NULL;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static void push(int fd,int d){
    Wait*w=malloc(sizeof*w); w->fd=fd; w->days=d; w->next=NULL;
    if(!q_tail) q_head=q_tail=w; else { q_tail->next=w; q_tail=w; }
}
static Wait* pop(void){
    if(!q_head) return NULL;
    Wait*w=q_head; q_head=q_head->next;
    if(!q_head) q_tail=NULL;
    return w;
}
static size_t q_len(void){
    size_t n=0; for(Wait*w=q_head; w; w=w->next) ++n; return n;
}

static void sendl(int fd,const char*s){ write(fd,s,strlen(s)); }

typedef struct{int id,days;} RArg;
static void* room(void*arg)
{
    RArg*ra=arg; sleep(ra->days*DAY_SEC);

    pthread_mutex_lock(&mtx);
    int guest=rooms[ra->id].fd;
    rooms[ra->id].busy=0; rooms[ra->id].fd=-1;
    pthread_mutex_unlock(&mtx);

    sendl(guest,"CHECKOUT\n"); close(guest);
    logger_log("Room %d became free", ra->id);

    pthread_mutex_lock(&mtx);
    Wait*w=pop();
    if(w){
        rooms[ra->id].busy=1; rooms[ra->id].days=w->days; rooms[ra->id].fd=w->fd;
        char msg[BUF]; snprintf(msg,sizeof msg,"ASSIGNED %d\n",ra->id); sendl(w->fd,msg);
        logger_log("Queued guest got room %d for %d day(s). Queue len now %zu",
                   ra->id, w->days, q_len());

        RArg*nr=malloc(sizeof*nr); *nr=(RArg){ra->id,w->days};
        pthread_t t; pthread_create(&t,NULL,room,nr); pthread_detach(t);
        free(w);
    }
    pthread_mutex_unlock(&mtx);
    free(ra); return NULL;
}

static void* guest(void*arg)
{
    int fd=*(int*)arg; free(arg);
    char buf[BUF]={0}; int n=read(fd,buf,sizeof buf-1);
    if(n<=0){close(fd);return NULL;}

    int d; if(sscanf(buf,"DAYS=%d",&d)!=1||d<=0){
        sendl(fd,"ERROR\n"); close(fd); return NULL; }

    pthread_mutex_lock(&mtx);
    int r=-1; for(int i=0;i<MAX_ROOMS;++i) if(!rooms[i].busy){r=i;break;}

    if(r>=0){
        rooms[r].busy=1; rooms[r].days=d; rooms[r].fd=fd;
        char ans[BUF]; snprintf(ans,sizeof ans,"ASSIGNED %d\n",r); sendl(fd,ans);
        logger_log("Guest assigned room %d for %d day(s)", r, d);

        RArg*ra=malloc(sizeof*ra); *ra=(RArg){r,d};
        pthread_t t; pthread_create(&t,NULL,room,ra); pthread_detach(t);
    }else{
        push(fd,d); sendl(fd,"WAIT\n");
        logger_log("Guest queued (%zu ahead)", q_len() - 1);
    }
    pthread_mutex_unlock(&mtx);
    return NULL;
}

int main(int argc,char*argv[])
{
    if(argc<3){
        fprintf(stderr,"Usage: %s <bind_ip> <port> [DAY_SEC]\n",argv[0]);return 1;}
    if(argc==4) DAY_SEC=atoi(argv[3]);

    if(logger_init(argv[1], LOG_PORT)!=0){perror("logger");return 1;}
    logger_log("Hotel server on %s:%s  (1 day = %d sec)", argv[1], argv[2], DAY_SEC);

    int l=socket(AF_INET,SOCK_STREAM,0);
    int opt=1; setsockopt(l,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof opt);
    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(atoi(argv[2]))};
    inet_pton(AF_INET, argv[1], &a.sin_addr);
    if(bind(l,(void*)&a,sizeof a)||listen(l,BACKLOG)){perror("bind/listen");return 1;}

    while(1){
        int *fd=malloc(sizeof(int)); *fd=accept(l,NULL,NULL);
        if(*fd<0){perror("accept"); free(fd); continue;}
        pthread_t t; pthread_create(&t,NULL,guest,fd); pthread_detach(t);
    }
}
