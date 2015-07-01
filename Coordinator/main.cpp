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

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

bool dump_jvm() {
    int ret = -1;
    int fd;
    
    printf("Dumping...\n");

    fd = open("/tmp/dump-test/", O_DIRECTORY);
    if (fd < 0) {
      fprintf(stderr, "ERROR: can't open images dir.\n");
      return false;
    }  
    
    ret = (*criu_init_opts)();
    if(ret) {
        fprintf(stderr, "ERROR: criu_init_opts failed.\n");
    }
    
    // TODO - check if this works without the service
    criu_set_service_address("/tmp/criu-service");
    // TODO - fix this
    criu_set_images_dir_fd(fd);     
    criu_set_log_level(4); 
    criu_set_log_file("dump.log");
    criu_set_leave_running(false);
    
    printf("Final Call...\n");
    ret = criu_dump();
    if (ret < 0) {
      fprintf(stderr, "ERROR: failed to dump jvm (error code = %d).\n", ret);
      return false;
    }
    
    return true;
}

int main(int argc, char** argv) {

    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[256];
    
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

    if (n < write(sockfd,PREPARE_MIGRATION,sizeof(PREPARE_MIGRATION))) {
        error("ERROR writing to socket");
    }
    bzero(buffer,256);
    n = read(sockfd,buffer,255);
    if (n < 0) { 
         error("ERROR reading from socket");
    }
    else if(n == 0) {
        close(sockfd);
        dump_jvm();
    }
    return 0;
}

