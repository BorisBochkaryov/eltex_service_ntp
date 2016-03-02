#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

char msg1[] = "Hello!";

int main()
{
    int sock;
    struct sockaddr_in addr,server;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0){
        printf("Error socket!\n");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(2016);
    addr.sin_addr.s_addr = inet_addr("192.168.0.103");
    sendto(sock, msg1, sizeof(msg1), 0,
           (struct sockaddr *)&addr, sizeof(addr)); // отправка серверу
    char buf[1024];
    int serlen = sizeof(server);
    recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&server, &serlen); // прием ответа от сервера
    printf("%s\n",buf);
    close(sock);
    return 0;
}
