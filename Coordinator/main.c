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

#define PREDUMP 1

#define DUMP_DIR "/tmp/dump"
#define DUMP_LOG "/tmp/dump.log"
#define PREDUMP_DIR "/tmp/pre-dump"
#define PREDUMP_LOG "/tmp/pre-dump.log"

bool dump_jvm(pid_t dumpee) {
    pid_t dumper = fork();
    char buf[8];
    
    sprintf(buf, "%lu", (unsigned long) dumpee);

    if(dumper == 0) {
        execl("/usr/local/sbin/criu", "/usr/local/sbin/criu", 
            "dump",
            "-D", DUMP_DIR, 
            "-d", 
            "-vvvv", 
            "-o", DUMP_LOG, 
            "-t", buf,
            "--remote", 
            "--prev-images-dir", PREDUMP_DIR,
            "--track-mem",
            "--shell-job",
            "--file-locks",
                            (char*) NULL);
    }
    else {
        printf("Launched dump (pid = %lu)\n", (unsigned long) dumper);
        if (waitpid(dumper, NULL, NULL) < 0) {
            fprintf(stderr, "Failed to wait for dump to finish\n");
            return false;
        }
        printf("Finished dump (pid = %lu)\n", (unsigned long) dumper);
        return true;
    }
}

bool pre_dump_jvm(pid_t dumpee) {
    pid_t dumper = fork();
    char buf[8];
    
    sprintf(buf, "%lu", (unsigned long) dumpee);

    if(dumper == 0) {
        execl("/usr/local/sbin/criu", "/usr/local/sbin/criu", 
            "pre-dump",
            "-D", PREDUMP_DIR, 
            "-d", 
            "-vvvv", 
            "-o", PREDUMP_LOG, 
            "-t", buf,
            "--remote",
            (char*) NULL);
    }
    else {
        printf("Launched pre-dump (pid = %lu)\n", (unsigned long) dumper);
        if (waitpid(dumper, NULL, NULL) < 0) {
            fprintf(stderr, "Failed to wait for dump to finish\n");
            return false;
        }
        printf("Finished pre-dump (pid = %lu)\n", (unsigned long) dumper);
        return true;
    }
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

#if PREDUMP
    
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
        if ((n = pre_dump_jvm(pid)) < 0) {
            fprintf(stderr, "ERROR: failed to dump jvm (error code = %d).\n", n);
        }
    }
    else {
        fprintf(stderr, "ERROR: agent should have just closed the connection.\n");
        return 0;
    }

    sleep(5); // Simulate data transfer delay.
    
#endif
    
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
    
    // Write Prepare Migration
    if (n < write(sockfd,PREPARE_MIGRATION,sizeof(PREPARE_MIGRATION))) {
        fprintf(stderr, "ERROR: writing to socket (prep migration)");
        return 0;
    }
    
    n = read(sockfd,buffer,256);
    if(n == 0) {
        close(sockfd);
        if ((n = dump_jvm(pid)) < 0) {
            fprintf(stderr, "ERROR: failed to dump jvm (error code = %d).\n", n);
        }
    }
    else {
        fprintf(stderr, "ERROR: agent should have just closed the connection.\n");
        return 0;
    }

    return 0;
}

