/* TODO - include open source banner. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <criu/criu.h>
#include <fcntl.h>

#define PREPARE_MIGRATION "01"
#define START_MIGRATION   "02"

#define MIGRATION_DIR "/tmp/dump-test/"
#define MIGRATION_LOG "dump.log"
#define MIGRATION_SERVICE "/var/run/criu-service.socket"

bool dump_jvm(pid_t pid) {
    int ret = -1;
    int fd;
    
    printf("Dumping...\n");

    fd = open(MIGRATION_DIR, O_DIRECTORY);
    if (fd < 0) {
      fprintf(stderr, "ERROR: can't open images dir.\n");
      return false;
    }  
    
    ret = criu_init_opts();
    if(ret) {
        fprintf(stderr, "ERROR: criu_init_opts failed.\n");
    }
    
    // TODO - check if this works without the service
    criu_set_service_address((char*)MIGRATION_SERVICE);
    criu_set_pid(pid);
    criu_set_remote(true);
    // TODO - fix this
    criu_set_images_dir_fd(fd);     
    criu_set_log_level(4); 
    criu_set_log_file((char*)MIGRATION_LOG);
    criu_set_leave_running(false);
    
    printf("Final Call...\n");
    return criu_dump();
}

bool pre_dump_jvm(pid_t pid) {
    // TODO
}

int prepare_client_socket(char* hostname, int port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
            fprintf(stderr, "ERROR: Unable to open socket (proxy)\n");
            return -1;
    }

    server = gethostbyname(hostname);
    if (server == NULL) {
            fprintf(stderr, "ERROR: Unable to get host by name (%s)\n", hostname);
            return -1;
    }

    bzero((char *) &serv_addr, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *) server->h_addr,
          (char *) &serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
            fprintf(stderr, "ERROR: Unable to connect (agent)\n");
            return -1;
    }

    return sockfd;
}

int main(int argc, char** argv) {

    int sockfd, n;
    char buffer[256];
    pid_t pid;
    
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       return 0;
    }

    // Pre-dump phase
    sockfd = prepare_client_socket(argv[1], atoi(argv[2]));
    if(sockfd < 0) {
        fprintf(stderr, "ERROR: unable to open socket to %s:%s\n", argv[1], argv[2]);
        return 0;
    }
    
    n = read(sockfd,&pid,sizeof(pid));
    if (n < 0) { 
         fprintf(stderr, "ERROR: reading from socket");
         return 0;
    }
    else if (n == 0) {
        fprintf(stderr,"ERROR: connection closed unexpectedly.\n");
        return 0;
    }
    
    printf("Process to pre-dump with pid %u\n", pid);
    
    // Write Prepare Migration
    if (n < write(sockfd,PREPARE_MIGRATION,sizeof(PREPARE_MIGRATION))) {
        fprintf(stderr, "ERROR: writing to socket (prep migration)");
        return 0;
    }
    
    n = read(sockfd,buffer,256);
    if(n == 0) {
        close(sockfd);
        /*
        if ((n = pre_dump_jvm(pid)) < 0) {
            fprintf(stderr, "ERROR: failed to dump jvm (error code = %d).\n", n);
        }
        */
    }
    else {
        fprintf(stderr, "ERROR: agent should have just closed the connection.\n");
        return 0;
    }
    
    sleep(2); // TODO - simulate transfer time
    
    // Dump phase
    sockfd = prepare_client_socket(argv[1], atoi(argv[2]));
    if(sockfd < 0) {
        fprintf(stderr, "ERROR: unable to open socket to %s:%s\n", argv[1], argv[2]);
        return 0;
    }

    n = read(sockfd,&pid,sizeof(pid));
    if (n < 0) { 
        fprintf(stderr,"ERROR: reading from socket");
        return 0;
    }
    else if (n == 0) {
        fprintf(stderr,"ERROR: connection closed unexpectedly.\n");
        return 0;
    }
    
    printf("Process to dump with pid %u\n", pid);
    
    // Write Start Migration
    if (n < write(sockfd,START_MIGRATION,sizeof(START_MIGRATION))) {
        fprintf(stderr, "ERROR: writing to socket (start migration)");
        return 0;
    }
    
    n = read(sockfd,buffer,256);
    if(n == 0) {
        close(sockfd);
        /*
        if ((n = dump_jvm(pid)) < 0) {
            fprintf(stderr, "ERROR: failed to dump jvm (error code = %d).\n", n);
        }
        */
    }
    else {
        fprintf(stderr, "ERROR: agent should have just closed the connection.\n");
        return 0;
    }
    
    return 0;
}

