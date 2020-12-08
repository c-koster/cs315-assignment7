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
    char *conn_remote_ip;
    uint16_t conn_remote_port;
    char *nickname;
    struct connection *next; // keep a pointer to the next node.
} connection;

// gvars: there is one rubber duck
pthread_mutex_t rubber_duck;

// there is a head node which is globally accessable
connection *conn_head = NULL;

// better be holding the rubber duck when you play wit this gvar (which is used
// to write formatted strings that broadcast to everyone)
//char shared_buf[BUF_SIZE];


// make a linked list of thread/clients full of file descriptor and client info
// that I need, and write the input string to all connections.

void *handle_connection(void *data);
connection *create_node(int fd, struct sockaddr_in *sa); // create a node
int broadcast(char *msg, int bytes, char *remote_ip, uint16_t remote_port, char *nickname); // visit every node
int remove_node(int fd); // delete a node from the linked list



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

        // NOTE
        conn = create_node(conn_fd,&remote_sa); // this needs rubber duck
        
        // put the thread here, and send the fd,SA,buffer into a new thread
        if (pthread_create(&child_thread, NULL, handle_connection, conn) < 0) {
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
    //char *s;

    char *remote_ip;
    uint16_t remote_port;
    //char *string_remote_port;
    int bytes_received;
    char *name_in;
    char name[BUF_SIZE];
    int new_line;
    char *end_ch = "\0";


    connection *conn = (connection *) data; // unpack all my variables
    //printf("conn: %d\n",conn->sa);
    // announce our communication partner  -- eventually to be a call to broadcast
    remote_ip = inet_ntoa(conn->sa.sin_addr);
    remote_port = ntohs(conn->sa.sin_port);
    printf("new connection from %s:%d\n", remote_ip, remote_port);
    // or here, but I would need to pass ip and port in !!
    //string_remote_port = (* char) remote_port;

    conn->conn_remote_ip = remote_ip;
    conn->conn_remote_port = remote_port;

    /* receive and echo data until the other end closes the connection */
    while ((bytes_received = recv(conn->fd, conn->buf, BUF_SIZE, 0)) > 0) {
        printf(".");
        fflush(stdout);
        //snprintf(conn->buf,BUF_SIZE,"(culton)>%s\n",conn->buf);
        
        if( strncmp(conn->buf, "/nick", 5) == 0 ) {
            name_in = conn->buf + 6;
            strcpy(name, name_in);
            //printf("who here: %s", name);
            new_line = strlen(name);
            name[new_line-1] = *end_ch;
            //printf("who here : %s\n", name);
            conn->nickname = name;
            //printf("name: %s\n", conn->nickname);
        }
     

        broadcast(conn->buf,bytes_received, conn->conn_remote_ip, conn->conn_remote_port, conn->nickname);

        //send(conn->fd, conn->buf, bytes_received, 0);
    }

    printf("\n");
    //snprintf(conn->buf,BUF_SIZE,"(culton)>%s\n",conn->buf);
    broadcast(conn->buf,bytes_received, conn->conn_remote_ip, conn->conn_remote_port, conn->nickname);

    //remove_node(conn->fd);

    // there are two reasons to close a connection
    // when the server quits
    // when the client sends a disconnect command

    return NULL;
}

// singly-linked-list operations -- -- --

/*
 * create_node - set head equal to a new node and link the previous head to its
 * next pointer
 * input:
 * -file descriptor you got from accepting
 * -pointer to the remote sockaddr filled with remote data
 * output: newly changed head global variable, which can also be accessed directly
 */
connection *create_node(int conn_fd, struct sockaddr_in *remote_sa)
{
    pthread_mutex_lock(&rubber_duck); // this looks like a critical section.

    // get a pointer to a new connection struct
    connection *new = calloc(1, sizeof(struct connection));

    //populate it
    memcpy(&new->sa, remote_sa, sizeof(struct sockaddr_in)); // copy sockaddr data
    new->fd = conn_fd;
    new->next = conn_head;
    conn_head = new;
    
    pthread_mutex_unlock(&rubber_duck); // unlock (done adding stuff)

    return conn_head; // and return usable connection data
}


/*
 * broadcast_all -
 * input:
 * -visit all items in the linked list and write msg to their fd/sockaddr
 * -maybe a file descriptor to ignore (the person who sent it)
 * output: whatever
 */
int broadcast(char* msg, int bytes, char *remote_ip, uint16_t remote_port, char *nickname)
{
    char full_msg[BUF_SIZE];

    connection *curr = conn_head;

    if ( nickname != NULL ) {
        sprintf(full_msg, "%s: %s", nickname, msg);
    } else {
        sprintf(full_msg, "%s:%d : %s", remote_ip, remote_port, msg);
    }
    while (curr != NULL) {
        
        send(curr->fd, full_msg, BUF_SIZE, 0);
        curr = curr->next;
    }
    memset(full_msg, 0, sizeof(full_msg));
    return 0;
}

/*
 * remove_node -
 * input: a file descriptor by which to
 * output: 0 on success, -1 if I didn't find anything (error).
 */
int remove_node(int fd)
{
    //connection *curr = conn_head;
    // get a pointer to the node with corresponding file descriptor
    // if there is none should return -1

    // set previous pointer too curr->next

    // close the connections

    // and free the memory
    //close(fd);
    return 0;
}
