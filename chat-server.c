/*
* chat-server.c
* Culton Koster and Victoria Toth
*/

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <pthread.h>

#define BACKLOG 10
#define BUF_SIZE 4096

typedef struct connection {
    int fd;
    struct sockaddr_in sa;
    // these come from the accept statement
    char buf[BUF_SIZE];
    // pass in a buffer so my broadcast_all function can write to threads
    char *nickname;
    struct connection *next; // keep a pointer to the next node.
} connection;

// gvars: there is one rubber duck
pthread_mutex_t rubber_duck;

// there is a head node which is globally accessable
connection *conn_head = NULL;

// make a linked list of thread/clients full of file descriptor and client info
// that I need, and write the input string to all connections.

void *handle_connection(void *data);
connection *create_node(int fd, struct sockaddr_in *sa); // create a node
int broadcast(char *msg); // visit every node
int remove_node(connection *fd); // delete a node from the linked list



int main(int argc, char *argv[])
{
    // create a socket for listening --
    char *listen_port;
    int listen_fd,conn_fd;
    struct addrinfo hints, *res;
    int rc;

    // setup threaded handling of a client --
    struct sockaddr_in remote_sa;
    socklen_t addrlen;
    connection *conn;
    pthread_t child_thread;


    listen_port = argv[1];
    /* create a socket */
    listen_fd = socket(PF_INET, SOCK_STREAM, 0);

    /* bind it to a port */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rc = getaddrinfo(NULL, listen_port, &hints, &res)) != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerror(rc));
        exit(1);
    }

    bind(listen_fd, res->ai_addr, res->ai_addrlen);

    // start listening
    listen(listen_fd, BACKLOG);

    // infinite loop of accepting new connections and handling them
    while (1) {
        /* accept a new connection (will block until one appears) */
        addrlen = sizeof(remote_sa);
        if ((conn_fd = accept(listen_fd, (struct sockaddr *) &remote_sa, &addrlen)) < 0) {
            perror("accept");
            exit(15);
        }

        conn = create_node(conn_fd,&remote_sa); // this needs rubber duck

        // put the thread here, and send the fd,SA,buffer into a new thread
        if (pthread_create(&child_thread, NULL, handle_connection, &conn) < 0) {
            perror("pthread_create");
            exit(11);
        }

    }
    // anything to do when all clients disconnect?
}

/*
 * handle_connection -
 * input: pointer to a fresh conn struct
 * output: whatever
 */
void *handle_connection(void *data)
{

    char *remote_ip;
    uint16_t remote_port;
    int bytes_received;

    connection *conn = (connection *) data; // unpack all my variables
    //printf("conn: %d\n",conn->sa);
    // announce our communication partner  -- eventually to be a call to broadcast
    remote_ip = inet_ntoa(conn->sa.sin_addr);
    remote_port = ntohs(conn->sa.sin_port);
    printf("new connection from %s:%d\n", remote_ip, remote_port);
    // or here, but I would need to pass ip and port in !!

    /* receive and echo data until the other end closes the connection */
    while ((bytes_received = recv(conn->fd, conn->buf, BUF_SIZE, 0)) > 0) {
        printf(".");
        fflush(stdout);

        /* send it back */
        send(conn->fd, conn->buf, bytes_received, 0);
    }
    printf("\n");

    // there are two reasons to close a connection
    // when the server quits
    // when the client sends a disconnect command

    close(conn->fd);
    return NULL;
}

// singly-linked-list operations -- -- --


/*
 * create_node -
 * input: pointer to a fresh conn struct
 * output: whatever
 */
connection *create_node(int conn_fd, struct sockaddr_in *remote_sa)
{
    if (conn_head == NULL) {
        conn_head = malloc(sizeof(struct connection));
        conn_head->next = NULL;
        printf("ptr:%p\n",conn_head);
        conn_head->sa = *remote_sa;
        conn_head->fd = conn_fd;

        return conn_head;
    }

    connection *curr = conn_head;
    while (curr->next != NULL) {
        curr = curr->next;
    }

    curr->next = malloc(sizeof(struct connection));
    curr = curr->next;
    curr->sa = *remote_sa;
    curr->fd = conn_fd;
    curr->next = NULL;

    return curr;
}


/*
 * broadcast_all -
 * input:
 * -visit all items in the linked list and write msg to their fd/sockaddr
 * -maybe a file descriiptor to ignore (the person who sent it)
 * output: whatever
 */
int broadcast(char *msg)
{
    return 0;
}

/*
 * remove_node -
 * input:
 * output: whatever
 */
int remove_node(connection *fd)
{
    return 0;
}
