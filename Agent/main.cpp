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

#define PREPARE_MIGRATION "01"
// TODO GET_PID?


/* Global static data */
static jvmtiEnv*      jvmti;
static int            gc_count;
static jrawMonitorID  lock;
static FILE*          log;
static jlong          min_migration_bandwidth = 1000;
// TODO - replace this with another mutex?
static bool           migration_in_progress = false; 
static pthread_mutex_t mutex;

/* Worker thread that waits for garbage collections */
static void JNICALL
worker(jvmtiEnv* jvmti, JNIEnv* jni, void *p) 
{
    jvmtiError err;
    
    fprintf(log, "GC worker started...\n");

    for (;;) {
        err = jvmti->RawMonitorEnter(lock);
        if (err != JVMTI_ERROR_NONE) {
	    fprintf(log, "ERROR: RawMonitorEnter failed (worker1), err=%d\n", err);
	    return;
	}
	while (gc_count == 0) {
	    err = jvmti->RawMonitorWait(lock, 0);
	    if (err != JVMTI_ERROR_NONE) {
		fprintf(log, "ERROR: RawMonitorWait failed (worker1), err=%d\n", err);
		jvmti->RawMonitorExit(lock);
		return;
	    }
	}
	gc_count = 0;
        
	jvmti->RawMonitorExit(lock);

	/* Perform arbitrary JVMTI/JNI work here to do post-GC cleanup */
	fprintf(log, "post-GarbageCollectionFinish actions...\n");
        if(migration_in_progress) {
          migration_in_progress = false;
          pthread_mutex_unlock(&mutex);
          // TODO - swith the mutex to alert the other thread to use the socket.          
        }
    }
}

/* Worker thread that waits for JVM migration */
static void JNICALL
worker2(jvmtiEnv* jvmti, JNIEnv* jni, void *p) {
    int sockfd, newsockfd;
    socklen_t clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;

    while(true) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            fprintf(log, "ERROR: Unable to open agent socket.\n");
        }
        
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(AGENT_SOCK_PORT);
        
        if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) { 
            fprintf(log, "ERROR: Unable to bind agent socket.\n");
        }
        else {
            fprintf(log, "Agent socket ready to accept connections.\n");
        }
        
        listen(sockfd,1);
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            fprintf(log, "ERROR: connection accept failed.\n");
        }
        else {
            fprintf(log, "Coordinator connection accepted.\n");
        }

        while(true) {
            bzero(buffer,256);
            if(read(newsockfd,buffer,255) < 0) {
                fprintf(log, "ERROR: failed to read from socket.\n");
                break;
            }
            if(strncmp(buffer, PREPARE_MIGRATION, sizeof(PREPARE_MIGRATION))) {
              fprintf(log, "Preparing Migration\n");
              jvmti->PrepareMigration(min_migration_bandwidth);
              migration_in_progress = true;
              pthread_mutex_lock(&mutex);
              close(newsockfd); // This will ack the other side.
              close(sockfd);    // This will not allow new connections.
              pthread_mutex_lock(&mutex);
            }
            else {
                fprintf(log, "ERROR: received unknown message. Ignoring...\n");
            }
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

    err = jvmti->RunAgentThread(alloc_thread(env), &worker, NULL,
	JVMTI_THREAD_MAX_PRIORITY);
    if (err != JVMTI_ERROR_NONE) {
	fprintf(log, "ERROR: RunAgentProc failed (worker1), err=%d\n", err);
    }
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
    fprintf(log, "GarbageCollectionStart...\n");
}

/* Callback for JVMTI_EVENT_GARBAGE_COLLECTION_FINISH */
static void JNICALL 
gc_finish(jvmtiEnv* jvmti_env) 
{
    jvmtiError err;
    
    fprintf(log, "GarbageCollectionFinish...\n");

    err = jvmti->RawMonitorEnter(lock);
    if (err != JVMTI_ERROR_NONE) {
	fprintf(log, "ERROR: RawMonitorEnter failed (worker1), err=%d\n", err);
    } else {
        gc_count++;
        err = jvmti->RawMonitorNotify(lock);
	if (err != JVMTI_ERROR_NONE) {
	    fprintf(log, "ERROR: RawMonitorNotify failed (worker1), err=%d\n", err);
	}
        err = jvmti->RawMonitorExit(lock);
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

    /* Create the necessary raw monitor */
    err = jvmti->CreateRawMonitor("lock", &lock);
    if (err != JVMTI_ERROR_NONE) {
	fprintf(log, "ERROR: Unable to create raw monitor: %d\n", err);
	return -1;
    }

    if((log = fopen("agent.log", "a")) == NULL) {
        fprintf(stderr, "ERROR: Unable to open log file.\n");
    }
    
    if (pthread_mutex_init(&mutex, NULL) != 0)
    {
        fprintf(log, "\n mutex init failed\n");
        return -11;
    }
    // TODO - explain why mutex is locked here.
    pthread_mutex_lock(&mutex);
    
    return 0;
}

/* Agent_OnUnload() is called last */
JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *vm)
{
}
