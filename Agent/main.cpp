/* TODO - include open source banner. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "jni.h"
#include "jvmti.h"

// TODO - put these definitions into a shared header.
// TODO - support for different ports. If one is taken, take the next.
#define AGENT_SOCK_PORT 9999
#define PROXY_SOCK_PORT 9991

#define PREPARE_MIGRATION "01"
#define GET_PID           "02"
#define START_MIGRATION   "03"


/* Global static data */
static jvmtiEnv*      jvmti;
static FILE*          log;
static jlong          min_migration_bandwidth = 1000;

static int prepare_migration = 0;
static int preparing_migration = 0;
static int start_migration = 0;
static int starting_migration = 0;
static int coord_sock = -1;

static int prepare_server_socket() {
    struct sockaddr_in serv_addr;
    int sockopt = 1;
        
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(log, "ERROR: Unable to open agent socket.\n");
        return -1;
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(AGENT_SOCK_PORT);

    if (setsockopt(
            sockfd, 
            SOL_SOCKET, 
            SO_REUSEADDR, 
            &sockopt, 
            sizeof(sockopt)) == -1) {
        fprintf(log, "ERROR: Unable to set SO_REUSEADDR\n");
        return -1;
    }

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) { 
        fprintf(log, "ERROR: Unable to bind agent socket.\n");
        return -1;
    }
    else {
        fprintf(log, "Agent socket ready to accept connections.\n");
        return -1;
    }

    if (listen(sockfd, 1)) {
        fprintf(log, "ERROR: Unable to listen image socket\n");
        return -1;
    }
    
    return sockfd;
}

static int prepare_client_socket() {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
            fprintf(log, "ERROR: Unable to open socket (proxy)\n");
            return -1;
    }

    server = gethostbyname("localhost");
    if (server == NULL) {
            fprintf(log, "ERROR: Unable to get host by name (localhost)\n");
            return -1;
    }

    bzero((char *) &serv_addr, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *) server->h_addr,
          (char *) &serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(PROXY_SOCK_PORT);

    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
            fprintf(log, "ERROR: Unable to connect (proxy)\n");
            return -1;
    }

    return sockfd;
}

/* Worker thread that waits for JVM migration */
static void JNICALL
worker2(jvmtiEnv* jvmti, JNIEnv* jni, void *p) {
    int sockfd = -1;
    char buffer[256];
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    while(true) {
        sockfd = prepare_server_socket();
        
        coord_sock = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (coord_sock < 0) {
            fprintf(log, "ERROR: connection accept failed.\n");
        }   
        else {
            fprintf(log, "Coordinator connection accepted.\n");
        }

        
        if(read(coord_sock,buffer,256) < 0) {
            fprintf(log, "ERROR: failed to read from socket.\n");
        }
        else if(!strncmp(buffer, PREPARE_MIGRATION, sizeof(PREPARE_MIGRATION))) {
            fprintf(log, "Prepare Migration\n");
            close(sockfd);
            prepare_migration = 1;
            jvmti->PrepareMigration(min_migration_bandwidth);
        }
        else if(!strncmp(buffer, START_MIGRATION, sizeof(START_MIGRATION))) {
            fprintf(log, "Start Migration\n");
            close(sockfd);
            start_migration = 1;
            jvmti->PrepareMigration(min_migration_bandwidth);
        }
        else if(!strncmp(buffer, GET_PID, sizeof(GET_PID))) {
            fprintf(log, "Getting PID\n");
            pid_t pid = getpid();
            write(coord_sock, &pid, sizeof(pid_t));
        }
        else {
            fprintf(log, "ERROR: received unknown message. Ignoring...\n");
        }
    }
}

/* Creates a new jthread */
static jthread 
alloc_thread(JNIEnv *env) {
    jclass    thrClass;
    jmethodID cid;
    jthread   res;

    thrClass = env->FindClass("java/lang/Thread");
    cid      = env->GetMethodID(thrClass, "<init>", "()V");
    res      = env->NewObject(thrClass, cid);
    return res;
}

/* Callback for JVMTI_EVENT_VM_INIT */
static void JNICALL 
vm_init(jvmtiEnv *jvmti, JNIEnv *env, jthread thread)
{
    jvmtiError err;
    
    fprintf(log, "VMInit...\n");

    err = jvmti->RunAgentThread(alloc_thread(env), &worker2, NULL,
	JVMTI_THREAD_MAX_PRIORITY);
    if (err != JVMTI_ERROR_NONE) {
	fprintf(log, "ERROR: RunAgentProc failed (worker2), err=%d\n", err);
    }
}

/* Callback for JVMTI_EVENT_GARBAGE_COLLECTION_START */
static void JNICALL 
gc_start(jvmtiEnv* jvmti_env) 
{
    if(prepare_migration) {
        prepare_migration = 0;
        preparing_migration = 1;
        fprintf(log, "GarbageCollectionStart (preparing migration)...\n");
    } 
    else if(start_migration) {
        start_migration = 0;
        starting_migration = 1;
        fprintf(log, "GarbageCollectionStart (starting migration)...\n");
    }
    else {
        fprintf(log, "GarbageCollectionStart...\n");
    }
}

/* Callback for JVMTI_EVENT_GARBAGE_COLLECTION_FINISH */
static void JNICALL 
gc_finish(jvmtiEnv* jvmti_env) 
{
    if(preparing_migration || starting_migration) {
        preparing_migration = starting_migration = 0;
        fprintf(log, "GarbageCollectionFinish (migration) ...\n");
        // TODO - call in the JVM and send the socket fd to write the free regions.
        // TODO - send free regions through socket to image-proxy (new socket)
        // TODO - close new socket (ack end)
        close(coord_sock); // (ack to proceed with dump/pre-dump)
        sleep(1);
    }
    else {
        fprintf(log, "GarbageCollectionFinish...\n");    
    }
}

/* Agent_OnLoad() is called first, we prepare for a VM_INIT event here. */
JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
    jint                rc;
    jvmtiError          err;
    jvmtiCapabilities   capabilities;
    jvmtiEventCallbacks callbacks;
    
    /* Get JVMTI environment */
    rc = vm->GetEnv((void **)&jvmti, JVMTI_VERSION);
    if (rc != JNI_OK) {
	fprintf(log, "ERROR: Unable to create jvmtiEnv, GetEnv failed, error=%d\n", rc);
	return -1;
    }

    /* Get/Add JVMTI capabilities */ 
    err = jvmti->GetCapabilities(&capabilities);
    if (err != JVMTI_ERROR_NONE) {
	fprintf(log, "ERROR: GetCapabilities failed, error=%d\n", err);
    }
    capabilities.can_generate_garbage_collection_events = 1;
    err = jvmti->AddCapabilities(&capabilities);
    if (err != JVMTI_ERROR_NONE) {
	fprintf(log, "ERROR: AddCapabilities failed, error=%d\n", err);
	return -1;
    }

    /* Set callbacks and enable event notifications */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMInit                  = &vm_init;
    callbacks.GarbageCollectionStart  = &gc_start;
    callbacks.GarbageCollectionFinish = &gc_finish;
    jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, 
			JVMTI_EVENT_VM_INIT, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, 
			JVMTI_EVENT_GARBAGE_COLLECTION_START, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, 
			JVMTI_EVENT_GARBAGE_COLLECTION_FINISH, NULL);

    if((log = fopen("agent.log", "w")) == NULL) {
        fprintf(stderr, "ERROR: Unable to open log file.\n");
    }
    
    return 0;
}

/* Agent_OnUnload() is called last */
JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *vm)
{
}
