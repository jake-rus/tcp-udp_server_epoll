/*
 * main.c
 *	Сервер tcp-udp с мультиплексированием epoll
 *	tcp создаются процессами, завершение отслеживается каналом
 *  Created on: 26 февр. 2018 г.
 *      Author: jake
 */

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>

#define TCPPORT 1234
#define UDPPORT 1235

int main(void)
{
	int i, tcpSock,ws, udpSock; // сокеты
	char buf[BUFSIZ];		// буфер для сообщений
	int count_client=0;		// счетчик клиентов
	pid_t pid;		// ид процессов tcp
	struct sockaddr_in tcp_serv;
	struct sockaddr_in udp_serv, from;
	int fromlen = sizeof(from);
	int epfd;				// структура мультиплексора сокетов
	struct epoll_event *events;// события для epoll
	int nr_events;			// количество произошедших событий epoll
	int fd[2];				// дескрипторы для pipe

	// обнуление длины и структур
	memset(&from,0,fromlen);
	bzero(&tcp_serv,sizeof(tcp_serv));
	bzero(&udp_serv,sizeof(udp_serv));

/* Инициализация tcp сокета */
	tcpSock = socket(AF_INET,SOCK_STREAM,0);
	tcp_serv.sin_port = htons(TCPPORT);
	tcp_serv.sin_family = AF_INET;
	tcp_serv.sin_addr.s_addr = htonl(INADDR_ANY);//0.0.0.0
	if (bind(tcpSock,(struct sockaddr*) &tcp_serv, sizeof(tcp_serv)) < 0)
	{
		perror("tcp binding error");
		exit(2);
	}
	listen(tcpSock,SOMAXCONN);
/*-------------------------*/
/* Инициализация udp сокета*/
	udpSock = socket(AF_INET,SOCK_DGRAM,0);
	if (udpSock < 0)
	{
		perror("socket error");
		exit(3);
	}
	udp_serv.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // или так
	udp_serv.sin_port = htons(UDPPORT);
	udp_serv.sin_family = AF_INET;
	if (bind(udpSock, (struct sockaddr*) &udp_serv, sizeof(udp_serv)) < 0)
	{
		perror("udp binding error");
		exit(4);
	}
/*-------------------------*/

/* Создаем канал между подпроцессами tcp и родителем */
	if(pipe(fd) < 0){
		perror("Can\'t create pipe");
		exit(5);
	}

/* Определение набора дескрипторов для epoll*/
	if ((epfd = epoll_create(3))<0){
		perror("epoll create");
		exit(6);}
	if (!(events = malloc(sizeof(struct epoll_event)*3))){
		perror("malloc");
		exit(7);}
	events->events = EPOLLIN;
	events->data.fd = udpSock;
	if (epoll_ctl(epfd,EPOLL_CTL_ADD,udpSock,events)){
		perror("epoll_ctl");
		exit(8);}
	events->data.fd = tcpSock;
	if (epoll_ctl(epfd,EPOLL_CTL_ADD,tcpSock,events)){
			perror("epoll_ctl");
			exit(9);}
	events->data.fd = fd[0];
	if (epoll_ctl(epfd,EPOLL_CTL_ADD,fd[0],events)){
			perror("epoll_ctl");
			exit(10);}

	printf("Waiting connect...\n");

/* Цикличная обработка подключений*/
	while(strstr(buf,"close->")==0)
	{
		if ((nr_events=epoll_wait(epfd,events,3,-1))==-1) { // ждем клиента
			perror("epoll_wait");
			free(events);
			exit(11);
		}
		for (i=0;i<nr_events;i++)
/* Проверка завершенного tcp соединения для приняти сигнала дочернего процесса*/
		if (events[i].data.fd == fd[0])
		{
			read(fd[0],&pid,sizeof(unsigned));
			waitpid(pid,0,0);
		}

/* Появление tcp клиента*/
		else if (events[i].data.fd == tcpSock) {
			ws=accept(tcpSock,NULL,NULL);
			if (0==(pid=fork())) { // процесс для tcp-клиента
				close(tcpSock); // закрыть дескр. слушающего сервера
				if(recv(ws,buf,BUFSIZ,0)<0)
				{
					perror("recv error");
					exit(12);
				}
				strcat(buf,"->");
				send(ws,buf,strlen(buf)+1,0);
				sleep(1);
				shutdown(ws,SHUT_RDWR);
				close(ws);
				pid=getpid();
				write(fd[1],&pid,sizeof(unsigned));
				exit(0);
			} else {// основной процесс ждет завершения tcp-процессов отдельными потоками
				printf("incoming tcp-client - %d\n",++count_client);
				close(ws);
			}
			continue;
		}

/* Появление udp клиента*/
		else if (events[i].data.fd == udpSock) {
			memset(buf,0,BUFSIZ);
			printf("incoming udp-client - %d\n",++count_client);
			if (recvfrom(udpSock,buf,sizeof(buf),0,(struct sockaddr*)&from,&fromlen) <0)
			{
				perror("recvfrom error");
				exit(13);
			}
			//printf("%s\n",buf);
			strcat(buf,"->");
			sendto(udpSock,buf,strlen(buf)+1,0,(struct sockaddr*)&from, fromlen);
		}
	}

/* Закрытие сокетов*/
	shutdown(tcpSock,SHUT_RDWR);
	shutdown(udpSock,SHUT_RDWR);
	close(tcpSock);
	close(udpSock);
	close(epfd);
	free(events);
	return 0;
}
