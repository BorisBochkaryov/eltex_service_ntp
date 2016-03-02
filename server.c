#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>

struct msgbuf { // структура сообщения для очереди сообщений
  long mtype; // тип сообщения 
  struct sockaddr_in client; // структура клиента для UDP сокета
  char ttext[1024]; // текст сообщения
};

enum States {waiting, recording, sending}; // {ожидание запроса, запись в очередь, отправка ответа клиенту}

int main(){
  key_t key_in = ftok("in",16L); // очередь запросов
  int msg_in = msgget(key_in, IPC_CREAT|0666);
  key_t key_out = ftok("out",16L); // очередь ответов
  int msg_out = msgget(key_out, IPC_CREAT|0666);

  int sock;
  if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){ // создаем сокет
    printf("Socket error!\n");
    return -1;
  }
  // структура адреса сокета
  struct sockaddr_in addr, cliaddr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(2016);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){ // закрепляем за адресом
    printf("Error bind!\n");
    return -1;
  }
  
  // парные сокеты для общения между потоками
  int sockets[2];
  if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0){
    printf("Error soketpair!\n");
    return -1;
  }

  // мультиплекс
  int epfd = epoll_create(5);
  if(epfd == -1){
    printf("Error epoll_create! %s\n",strerror(errno));
    return -1;
  }
  struct epoll_event ev;
  ev.data.fd = sock;
  ev.events = EPOLLIN | EPOLLET | EPOLLHUP;
  if(epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev) == -1){ // добавляем дескриптор сокета в структуру
    printf("Error epoll_ctl socket!\n");
    return -1;
  }
  ev.data.fd = sockets[0];
  ev.events = EPOLLIN | EPOLLET | EPOLLHUP;
  if(epoll_ctl(epfd, EPOLL_CTL_ADD, sockets[0], &ev) == -1){ // добавляем дескриптор одного из парных сокетов в структуру
    printf("Error epoll_ctl sockets[0] (parent)!\n");
    return -1;
  }
  // создаем дочерний поток для обработки сообщений в очереди
  int pid = fork();
  if(pid == 0){ // отвечает за обработку запросов из очереди запросов
    close(sockets[0]);
    struct msgbuf buf;
    while(msgrcv(msg_in,&buf,sizeof(buf),3000L,0) > 0){
      // получение времени на сервере
      time_t temp;
      struct tm *timeinfo;
      time(&temp);
      timeinfo = localtime(&temp);
      strftime(buf.ttext,1024,"%c",timeinfo);
      msgsnd(msg_out,&buf,sizeof(buf),0); // запись в очередь ответов
      write(sockets[1],"send",sizeof("send")); // оповещаем родителя о готовности ответа
    }
    close(sockets[1]);

  } else if(pid == -1){ // ошибка создания дочернего потока
    printf("Error fork!\n");
    return -1;

  } else { // в родительском потоке
    close(sockets[1]);
    int current = waiting; // текущее состояние
    while(1){
      switch(current){
        case waiting: { // ожидание событий
          struct epoll_event events[1024];
          int ready = epoll_wait(epfd, events, 1024, -1); // ожидаем событие
          if(ready == -1){
            printf("Error epoll_wait!\n");
            return -1;
          }
          int i;
          for(i = 0; i < ready; i++){
            if(events[i].data.fd == sock){ // если произошло событие на сокете сервера
              current = recording;
            } else if(events[i].data.fd == sockets[0]){ // если произошло событие на парном сокете (внутреннем)
              current = sending;
            }
          }
        } break;

        case recording: { // получение запроса и запись его в очередь
          char bufer[1024];
          int clilen = sizeof(cliaddr);
          recvfrom(sock, bufer, sizeof(bufer), 0, (struct sockaddr*)&cliaddr, &clilen); // прием от клиентов
          // заполняем структуру для сообщения
          struct msgbuf buf;
          buf.mtype = 3000L;
	  buf.client = cliaddr;
          memset(buf.ttext,0,1024);
	  msgsnd(msg_in,&buf,sizeof(buf),0); // записываем в очередь
          current = waiting;
        } break;

        case sending: { // отправка ответа клиенту
          // получаем сообщение из очереди на отправку
          struct msgbuf buf;
          msgrcv(msg_out,&buf,sizeof(buf),3000L,0);
          //printf("Время для отправки клиенту: %s!\n",buf.ttext);
          int clilen = sizeof(buf.client);
          sendto(sock, buf.ttext, sizeof(buf.ttext), 0, (struct sockaddr*)&buf.client, clilen); // отправка клиенту, который делал запрос
          // очищаем парный сокет
          char temp[25] = "";
	  read(sockets[0],temp,sizeof(temp));
          current = waiting;
        } break;
      }
    } 
    close(sockets[0]);
  }
  return 0;
}
