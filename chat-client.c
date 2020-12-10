/*
 * chat-client.c
 * Culton Koster and Victoria Toth
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define BUF_SIZE 4096

#define TIME_BUF 100

typedef struct reciever {
    int c_fd;
} reciever;

void *recieve_messages(void *data);

int main(int argc, char *argv[])
{
    char *dest_hostname, *dest_port;
    struct addrinfo hints, *res;
    int conn_fd;
    char buf[BUF_SIZE];
    int n;
    int rc;

    pthread_t child_thread;

    dest_hostname = argv[1];
    dest_port     = argv[2];

    /* create a socket */
    conn_fd = socket(PF_INET, SOCK_STREAM, 0);

    /* client usually doesn't bind, which lets kernel pick a port number */

    /* but we do need to find the IP address of the server */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if ((rc = getaddrinfo(dest_hostname, dest_port, &hints, &res)) != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerror(rc));
        exit(1);
    }

    /* connect to the server */
    if (connect(conn_fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        exit(2);
    }

    printf("Connected\n");

    // use struct to send conn_fd to child thread
    reciever *new = calloc(1, sizeof(struct reciever));
    new->c_fd = conn_fd;
    if (pthread_create(&child_thread, NULL, recieve_messages, new) < 0) {
            perror("pthread_create");
            exit(11);
    }

    // infinite loop of reading from terminal and sending the data.
    // pressing CTRL-D will quit you out of here.
    while ((n = read(0, buf, BUF_SIZE)) > 0) {

        send(conn_fd, buf, n, 0);

    }
    printf("Exiting.\n");
    close(conn_fd);
}


void *recieve_messages(void *data) {
    char buf[BUF_SIZE];
    int n;
    reciever *conn_fd = (reciever *) data;

    // timestamp setup
    char time_string[TIME_BUF];
    time_t t;
    struct tm *tmp;
    t = time(NULL);

    while ((n = recv(conn_fd->c_fd, buf, BUF_SIZE, 0)) > 0) {
        buf[n] = '\0';  // null-terminate string before printing

        tmp = localtime(&t);
        strftime(time_string, TIME_BUF, "%H:%M:%S", tmp);
        printf("%s: %s",time_string,buf);
    }
    return NULL;
}

// name = recv(conn_fd->c_fd, name_buf, BUF_SIZE,0);
        // msg = recv(conn_fd->c_fd, msg_buf, BUF_SIZE, 0);
        // /* null-terminate string before printing it */
        // name_buf[name] = '\0';
        // msg_buf[msg] = '\0';
        // puts(name_buf);
        // printf(": ");
        // puts(msg_buf);
