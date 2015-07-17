#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <utlist.h>
#include <time.h>

#define DEFAULT_LISTEN 50
#define PATHLEN 32
#define DUMP_FINISH "DUMP_FINISH"
#define PAGESIZE 4096
#define BUF_SIZE PAGESIZE*250
// TODO - this may be problematic because of double evaluation...
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

typedef struct rbuf {
    char buffer[BUF_SIZE];
    int nbytes; // How many bytes are in the buffer.
    struct rbuf *next, *prev;
} remote_buffer;

typedef struct rimg {
    char path[PATHLEN];
    int src_fd;
    int dst_fd;
    struct rimg *next, *prev;
    pthread_t worker;
    remote_buffer* buf_head;
    
} remote_image;

static remote_image *head = NULL;
static int sockfd = -1;
static sem_t semph;
static int server_port;
static char* dst_host;
static int dst_port;

void* buffer_remote_image(void* ptr) {
    int n, curr_offset;
    remote_image* rimg = (remote_image*) ptr;
    int src_fd = rimg->src_fd;
    int dst_fd = rimg->dst_fd;
    remote_buffer* curr_buf = rimg->buf_head;
    
    while(1) {
        n = read(   src_fd, 
                    curr_buf->buffer + curr_buf->nbytes, 
                    BUF_SIZE - curr_buf->nbytes);
        if (n == 0) {
            close(src_fd);
            printf("Finished receiving %s. Forwarding...\n", rimg->path);
            break;
        }
        else if (n > 0) {
            curr_buf->nbytes += n;
            if(curr_buf->nbytes == BUF_SIZE) {
                remote_buffer* buf = malloc(sizeof (remote_buffer));
                if(buf == NULL) {
                    fprintf(stderr,"Unable to allocate remote_buffer structures\n");
                }
                buf->nbytes = 0;
                DL_APPEND(rimg->buf_head, buf);
                curr_buf = buf;
            }
            
        }
        else {
            fprintf(stderr,"Read on %s socket failed\n", rimg->path);
            return NULL;
        }
    }
    
    curr_buf = rimg->buf_head;
    curr_offset = 0;
    while(1) {
        n = write(
                    dst_fd, 
                    curr_buf->buffer + curr_offset, 
                    MIN(BUF_SIZE, curr_buf->nbytes) - curr_offset);
        if(n > -1) {
            curr_offset += n;
            if(curr_offset == BUF_SIZE) {
                curr_buf = curr_buf->next;
                curr_offset = 0;
            }
            else if(curr_offset == curr_buf->nbytes) {
                printf("Finished forwarding %s. Done.\n", rimg->path);
                close(dst_fd);
                break;
            }
        }
        else {
             fprintf(stderr,"Write on %s socket failed (n=%d)\n", rimg->path, n);
        }
    }
    return NULL;
}

void* accept_remote_image_connections(void* null) {
    socklen_t clilen;
    int src_fd, dst_fd, n;
    struct sockaddr_in cli_addr, serv_addr;
    clilen = sizeof (cli_addr);
    struct hostent *restore_server;
    time_t t;

    while (1) {
        src_fd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (src_fd < 0) {
            fprintf(stderr,"Unable to accept checkpoint image connection\n");
            continue;
        }

        remote_image* img = malloc(sizeof (remote_image));
        if (img == NULL) {
            fprintf(stderr,"Unable to allocate remote_image structures\n");
            return NULL;
        }
        
        remote_buffer* buf = malloc(sizeof (remote_buffer));
        if(buf == NULL) {
            fprintf(stderr,"Unable to allocate remote_buffer structures\n");
            return NULL;
        }

        n = read(src_fd, img->path, PATHLEN);
        if (n < 0) {
            fprintf(stderr,"Error reading from checkpoint remote image socket\n");
            continue;
        } else if (n == 0) {
            fprintf(stderr,"Remote checkpoint image socket closed before receiving path\n");
            continue;
        }
        
        
        dst_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (dst_fd < 0) {
            fprintf(stderr,"Unable to open recover image socket\n");
            return NULL;
        }

        restore_server = gethostbyname(dst_host);
        if (restore_server == NULL) {
            fprintf(stderr,"Unable to get host by name (%s)\n", dst_host);
            return NULL;
        }

        bzero((char *) &serv_addr, sizeof (serv_addr));
        serv_addr.sin_family = AF_INET;
        bcopy(  (char *) restore_server->h_addr,
                (char *) &serv_addr.sin_addr.s_addr,
                restore_server->h_length);
        serv_addr.sin_port = htons(dst_port);

        n = connect(dst_fd, (struct sockaddr *) &serv_addr, sizeof (serv_addr));
        if (n < 0) {
            fprintf(stderr,"Unable to connect to remote restore host %s: %s\n", dst_host, strerror(errno));
            return NULL;
        }

        if (write(dst_fd, img->path, PATHLEN) < 1) {
            fprintf(stderr,"Unable to send path to remote image connection\n");
            return NULL;
        }
        
        if (!strncmp(img->path, DUMP_FINISH, sizeof (DUMP_FINISH))) {
            printf("Dump side is finished!\n");
            free(img);
            free(buf);
            close(src_fd);
            sem_post(&semph);
            return NULL;
        }
       
        img->src_fd = src_fd;
        img->dst_fd = dst_fd;
        img->buf_head = NULL;
        buf->nbytes = 0;
        DL_APPEND(img->buf_head, buf);
        
        if (pthread_create( &img->worker, 
                            NULL, 
                            buffer_remote_image, 
                            (void*) img)) {
                fprintf(stderr,"Unable to create socket thread\n");
                return NULL;
        } 
        
        time(&t);
        printf("Reveiced %s, from %d to %d\n (%s)", img->path, img->src_fd, img->dst_fd, ctime(&t));
        DL_APPEND(head, img);
    }
}

int main(int argc, char** argv) {
    int sockopt = 1;
    struct sockaddr_in serv_addr;
    pthread_t sock_thr;
    
    if(argc != 4) {
        fprintf(stderr, "Usage: ./sock-proxy <local proxy server port> <destination host> <destination port>");
        return 0;
    }
    
    server_port = atoi(argv[1]);
    dst_host = argv[2];
    dst_port = atoi(argv[3]);
    printf ("Local Port %d, Remote Host %s:%d\n", server_port, dst_host, dst_port);
    
    if (sem_init(&semph, 0, 0) != 0) {
        fprintf(stderr, "Remote image connection semaphore init failed\n");
        return -1;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Unable to open image socket\n");
        return -1;
    }

    bzero((char *) &serv_addr, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(server_port);

    if (setsockopt(
            sockfd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &sockopt,
            sizeof (sockopt)) == -1) {
        fprintf(stderr, "Unable to set SO_REUSEADDR\n");
        return -1;
    }

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
        fprintf(stderr, "Unable to bind image socket\n");
        return -1;
    }

    if (listen(sockfd, DEFAULT_LISTEN)) {
        fprintf(stderr, "Unable to listen image socket\n");
        return -1;
    }

    if (pthread_create(
            &sock_thr, NULL, accept_remote_image_connections, NULL)) {
        fprintf(stderr, "Unable to create socket thread\n");
        return -1;

    }
    
    sem_wait(&semph);
    
    remote_image *elt, *tmp;
    DL_FOREACH_SAFE(head,elt,tmp) {
        pthread_join(elt->worker, NULL);
        DL_DELETE(head,elt);
    }
    
    return 0;
}
