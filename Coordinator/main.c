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

#define AGENT_SOCK_PORT 9999

#define PREPARE_MIGRATION "01"
#define GET_PID           "02"

#define MIGRATION_DIR "/tmp/dump-test/"
#define MIGRATION_LOG "dump.log"
#define MIGRATION_SERVICE "/var/run/criu-service.socket"

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

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
    // TODO - fix this
    criu_set_images_dir_fd(fd);     
    criu_set_log_level(4); 
    criu_set_log_file((char*)MIGRATION_LOG);
    criu_set_leave_running(false);
    
    printf("Final Call...\n");
    return criu_dump();
}

int main(int argc, char** argv) {

    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[256];
    pid_t pid;
    
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }
    
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("ERROR opening socket");
    }
    
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        error("ERROR, no such host\n");
    }
    
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(portno);
    
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
        error("ERROR connecting");
    }

    // Write Get Pid
    if (write(sockfd,GET_PID,sizeof(GET_PID)) < 0) {
        error("ERROR writing to socket (get pid)");
    }
    
    // Read Result (expected a pid)
    n = read(sockfd,&pid,sizeof(pid));
    if (n < 0) { 
         error("ERROR reading from socket");
    }
    else if (n == 0) {
        fprintf(stderr, "ERROR connection closed unexpectedly.\n");
    }
    
    printf("Process to dump with pid %u\n", pid);
    
    // Write Prepare Migration
    if (n < write(sockfd,PREPARE_MIGRATION,sizeof(PREPARE_MIGRATION))) {
        error("ERROR writing to socket (prep migration)");
    }
    
    // Read Result (expected a connection close)
    bzero(buffer,256);
    n = read(sockfd,buffer,255);
    if (n < 0) { 
         error("ERROR reading from socket");
    } 
    else if(n == 0) {
        close(sockfd);
        if ((n = dump_jvm(pid)) < 0) {
            fprintf(stderr, "ERROR: failed to dump jvm (error code = %d).\n", n);
        }
    }
    else {
        fprintf(stderr, "ERROR: agent should have just closed the connection.\n");
    }
    
    return 0;
}

