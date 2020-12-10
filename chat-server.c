/*
 * chat-server.c
 * Culton Koster and Victoria Toth
 */

#include <signal.h>

#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <unistd.h>

#include <stdio.h>
#include <string.h>


#include <pthread.h>

#define BACKLOG 10
#define BUF_SIZE 4096

typedef struct connection {
    int fd;
    struct sockaddr_in sa;
    // these come from the accept statement
    char buf[BUF_SIZE];
    char buf_out[BUF_SIZE];
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
char msg_out[BUF_SIZE];

// better be holding the rubber duck when you play wit this gvar (which is used
// to write formatted strings that broadcast to everyone)
//char shared_buf[BUF_SIZE];


// make a linked list of thread/clients full of file descriptor and client info
// that I need, and write the input string to all connections.

void *handle_connection(void *data);

// SLL ops
connection *create_node(int fd, struct sockaddr_in *sa); // create a node
int broadcast(char *msg, int bytes); // visit every node
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

        conn = create_node(conn_fd,&remote_sa);

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
    int n;


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
        //printf(".");
        fflush(stdout);
        //snprintf(conn->buf,BUF_SIZE,"(culton)>%s\n",conn->buf);

        if ( strncmp(conn->buf, "/nick", 5) == 0 ) {
            name_in = conn->buf + 6;
            strcpy(name, name_in);
            //printf("who here: %s", name);
            new_line = strlen(name);
            name[new_line-1] = *end_ch;
            //printf("who here : %s\n", name);
            conn->nickname = name;
            n = snprintf(conn->buf_out,BUF_SIZE,"User (%s:%d) is now known as %s.\n",  conn->conn_remote_ip, conn->conn_remote_port, conn->nickname);
            printf("%s\n",conn->buf_out);
            broadcast(conn->buf_out,n);

        } else {
            // format a string for printing
            n = snprintf(conn->buf_out, BUF_SIZE, "%s: %s", (conn->nickname != NULL) ? conn->nickname : "unknown", conn->buf);
            //printf("sending %d bytes over\n",n);
            broadcast(conn->buf_out,n);

        }
    }

    // if they are out of this loop -- we should disconnect
    n = snprintf(conn->buf_out, BUF_SIZE, "User %s (%s:%d) has disconnected.", (conn->nickname != NULL) ? conn->nickname : "unknown", conn->conn_remote_ip, conn->conn_remote_port);
    printf("%s\n",conn->buf_out);
    broadcast(conn->buf_out,n);
    remove_node(conn->fd);

    // there are two reasons to close a connection
    // when the server quits

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
 * broadcast_all - should be locked before calling (Before writing a string to the global buffer)
 * input: a pointer to the string we would like to write to everyone
 * -visit all items in the linked list and write msg to their fd/sockaddr
 * -maybe a file descriptor to ignore (the person who sent it)
 * output: whatever
 */
int broadcast(char *msg, int bytes)
{
    connection *curr = conn_head;

    while (curr != NULL) {
        send(curr->fd, msg, bytes, 0);
        curr = curr->next;
    }
    memset(msg, '\0', BUF_SIZE);

    return 0;
}

/*
 * remove_node -
 * input: a file descriptor by which to locate the node
 * output: 0 on success, -1 if I didn't find anything (error).
 */
int remove_node(int fd)
{

    pthread_mutex_lock(&rubber_duck); // this looks like a critical section.

    connection *curr = conn_head;
    if (curr == NULL) {
        return -1;
    }
    // get a pointer to the node with corresponding file descriptor
    while (curr->fd != fd) {
        curr = curr->next;
    }

    // if there is none should return -1

    // close the connection
    //printf("I am removing node %s:%d\n",curr->conn_remote_ip,curr->conn_remote_port);

    close(curr->fd);
    // and free the memory
    free(curr);
    curr = curr->next;
    // set previous pointer to curr->next

    pthread_mutex_unlock(&rubber_duck); // unlock
    return 0;
}

/*
 * remove_all_nodes - call me when you press control d
 * input: nothing
 * -visit all items in the linked list and free/close each
 * output: whatever, 0?
 */
int remove_all_nodes() {
    char *s = "Connection closed by host.";
    int s_length = 27;
    broadcast(s,s_length);

    pthread_mutex_lock(&rubber_duck); // very very critical

    connection *curr = conn_head;
    connection *temp = conn_head;

    while (curr != NULL) {
        temp = curr->next; // don't you dare use after free
        close(curr->fd);
        free(curr);
        curr = temp;
    }
    pthread_mutex_unlock(&rubber_duck);

    return 0;
}
