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
#include <errno.h>


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

int remove_all_nodes(); // called by the SIGINT handler to quit nicely


// handle CTRL-C by cleaning up nicely with a descriptive message and exiting
void handler(int signum)
{
    char *s = "Connection closed by host.\n";
    broadcast(s,strlen(s));
    exit(0);
}


int main(int argc, char *argv[])
{
    // signal handler --
    struct sigaction sig_act;

    /* set up the struct that defines what to do on receipt of a signal */
    memset(&sig_act, '\0', sizeof(sig_act));
    sig_act.sa_handler = handler;
    sigemptyset(&sig_act.sa_mask);
    sig_act.sa_flags = 0;

    // we want to handle SIGINT/CTRL-C
    if (sigaction(SIGINT, &sig_act, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }

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

    char *remote_ip;
    uint16_t remote_port;

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

    while ((bytes_received = read(conn->fd, conn->buf, BUF_SIZE)) > 0) {

        conn->buf[bytes_received] = '\0';
        // i before e except after c

        if (strncmp(conn->buf, "/nick ", 6) == 0) {

            // set a nickname -- thanks vic
            name_in = conn->buf + 6;
            strncpy(name, name_in,BUF_SIZE);
            new_line = strlen(name);
            name[new_line-1] = *end_ch;
            conn->nickname = name;

            n = snprintf(conn->buf_out, BUF_SIZE, "User (%s:%d) is now known as %s.\n", conn->conn_remote_ip, conn->conn_remote_port, conn->nickname);
            printf("%s\n",conn->buf_out);

        } else {
            // format a string for printing
            n = snprintf(conn->buf_out, BUF_SIZE, "%s: %s", (conn->nickname != NULL) ? conn->nickname : "unknown", conn->buf);
        }
        broadcast(conn->buf_out, n);
        memset(conn->buf, '\0', BUF_SIZE);

        errno = 0;
    }

    if (errno != 0) { // check if my loop exited because of an error
        perror("read");
        exit(4);
    }

    // if they are out of the above loop -- we should disconnect
    n = snprintf(conn->buf_out, BUF_SIZE, "User %s (%s:%d) has disconnected.\n", (conn->nickname != NULL) ? conn->nickname : "unknown", conn->conn_remote_ip, conn->conn_remote_port);
    printf("%s",conn->buf_out);
    broadcast(conn->buf_out,n);

    remove_node(conn->fd);

    return NULL;
}


// singly-linked-list operations -- -- --


/*
 * broadcast_all - should be locked before calling (Before writing a string to the global buffer)
 * input: a pointer to the string we would like to write to everyone
 * -visit all items in the linked list and write msg to their fd/sockaddr
 * -maybe a file descriptor to ignore (the person who sent it)
 * output: whatever
 */
int broadcast(char *msg, int bytes)
{
    if (pthread_mutex_lock(&rubber_duck) < 0) {
        perror("pthread_mutex_lock");
        exit(5);
    }
    connection *curr = conn_head;

    while (curr != NULL) {
        if (write(curr->fd, msg, bytes) < 0){
            perror("write");
        };
        //printf("writing to fd=%d\n",curr->fd);
        curr = curr->next;
    }

    if (pthread_mutex_unlock(&rubber_duck) < 0) {
        perror("pthread_mutex_unlock");
        exit(5);
    }

    return 0;
}

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
    if (pthread_mutex_lock(&rubber_duck) < 0) {
        perror("pthread_mutex_lock");
        exit(5);
    } // this looks like a critical section.

    // get a pointer to a new connection struct
    connection *new = calloc(1, sizeof(struct connection));
    if (new == NULL) {
        perror("malloc");
        exit(1);
    }

    //populate it
    memcpy(&new->sa, remote_sa, sizeof(struct sockaddr_in)); // copy sockaddr data
    new->fd = conn_fd;
    new->next = conn_head;
    conn_head = new;


    if (pthread_mutex_unlock(&rubber_duck) < 0) {
        perror("pthread_mutex_unlock");
        exit(5);
    } // unlock (done adding stuff)

    return conn_head; // and return usable connection data
}

/*
 * remove_node -
 * input: a file descriptor by which to locate the node
 * output: 0 on success, -1 if I didn't find anything (error).
 */
int remove_node(int fd)
{

    if (pthread_mutex_lock(&rubber_duck) < 0) {
        perror("pthread_mutex_lock");
        exit(5);
    }  // this looks like a critical section.

    connection *prev = NULL;
    connection *curr = conn_head;
    connection *temp;
    if (curr == NULL) {
        return -1;
    }

    // get a pointer to the node with corresponding file descriptor
    while (curr->fd != fd) {
        prev = curr;
        curr = curr->next;

        if (curr == NULL) {
            return -1; //do I fall off the end of the list while looking
        }
    }

    // close the connection
    //printf("I am removing node %s:%d\n",curr->conn_remote_ip,curr->conn_remote_port);

    if (close(curr->fd) < 0) {
        perror("close");
        exit(6);
    }
    temp = curr->next; // don't you dare use after free
    free(curr);

    if (prev == NULL) { // if there was no previous node ...
        conn_head = temp;
    } else { // set previous pointer to curr->next
        prev->next = temp;
    }

    if (pthread_mutex_unlock(&rubber_duck) < 0) {
        perror("pthread_mutex_unlock");
        exit(5);
    }
    return 0;
}

/*
 * remove_all_nodes - call me when you press control d
 * input: nothing
 * -visit all items in the linked list and frees/closes each
 * output: whatever, 0?
 */
/*
int remove_all_nodes() {

    if (pthread_mutex_lock(&rubber_duck) < 0) {
        perror("pthread_mutex_lock");
        exit(5);
    } // very very critical

    connection *curr = conn_head;
    while (curr != NULL) {

        printf("closing fd=%d\n",curr->fd);
        if (close(curr->fd) < 0) {
            perror("close");
            exit(6);
        }
        curr = curr->next; // don't you dare use after free
    }

    if (pthread_mutex_unlock(&rubber_duck) < 0) {
        perror("pthread_mutex_unlock");
        exit(5);
    }
    return 0;
} */
