#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc,char*argv[])
{
    if(argc!=3){fprintf(stderr,"usage: %s <ip> <log_port>\n",argv[0]);return 1;}
    int fd = socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(atoi(argv[2]))};
    inet_pton(AF_INET, argv[1], &a.sin_addr);
    if(connect(fd,(void*)&a,sizeof a)){perror("connect");return 1;}

    char buf[256]; int n;
    while((n=read(fd,buf,sizeof buf))>0) fwrite(buf,1,n,stdout);
    return 0;
}
