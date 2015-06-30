/*
 * @(#)gctest.c	1.5 04/07/27
 * 
 * Copyright (c) 2004 Sun Microsystems, Inc. All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * -Redistribution of source code must retain the above copyright notice, this
 *  list of conditions and the following disclaimer.
 * 
 * -Redistribution in binary form must reproduce the above copyright notice, 
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 * 
 * Neither the name of Sun Microsystems, Inc. or the names of contributors may 
 * be used to endorse or promote products derived from this software without 
 * specific prior written permission.
 * 
 * This software is provided "AS IS," without a warranty of any kind. ALL 
 * EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES, INCLUDING
 * ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * OR NON-INFRINGEMENT, ARE HEREBY EXCLUDED. SUN MIDROSYSTEMS, INC. ("SUN")
 * AND ITS LICENSORS SHALL NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE
 * AS A RESULT OF USING, MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS
 * DERIVATIVES. IN NO EVENT WILL SUN OR ITS LICENSORS BE LIABLE FOR ANY LOST 
 * REVENUE, PROFIT OR DATA, OR FOR DIRECT, INDIRECT, SPECIAL, CONSEQUENTIAL, 
 * INCIDENTAL OR PUNITIVE DAMAGES, HOWEVER CAUSED AND REGARDLESS OF THE THEORY 
 * OF LIABILITY, ARISING OUT OF THE USE OF OR INABILITY TO USE THIS SOFTWARE, 
 * EVEN IF SUN HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * You acknowledge that this software is not designed, licensed or intended
 * for use in the design, construction, operation or maintenance of any
 * nuclear facility.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <csignal>
#include <pthread.h>

#include "jni.h"
#include "jvmti.h"

#include <criu/criu.h>

/* Global static data */
static jvmtiEnv*      jvmti;
static int            gc_count;
static jrawMonitorID  lock;
static FILE*          log;
static jlong          min_migration_bandwidth = 1000;
static bool           migration_in_progress = false;

static pthread_mutex_t mutex;

void signalHandler( int signum )
{
    // TODO - use a simple C lock?
    printf("Preparing Migration...\n");    
    pthread_mutex_unlock(&mutex);
}

bool dump_jvm() {
    int ret = -1;
    
    printf("Dumping...\n");
    if(!criu_init_opts()) {
        fprintf(log, "ERROR: criu_init_opts failed.\n");
    }
    /*
    criu_set_service_address(NULL); // TODO - check if this is okey. 
    criu_set_images_dir_fd(0);     // TODO - fix this
    criu_set_log_level(4); 
    criu_set_log_file("dump.log");                                  
    criu_set_leave_running(false);
    ret = criu_dump();                                              
    if (ret < 0) {
      fprintf(log, "ERROR: failed to dump jvm (error code = %d).\n", ret);
    }
    else {
        fprintf(log, "Done Dumping JVM\n");
    }
    */
}

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
    }
}

/* Worker thread that waits for JVM migration */
static void JNICALL
worker2(jvmtiEnv* jvmti, JNIEnv* jni, void *p) 
{
    for (;;) {
        pthread_mutex_lock(&mutex);
        migration_in_progress = true;
        jvmti->PrepareMigration(min_migration_bandwidth);
        // TODO - are we inside a safepoint pause?
        //
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
        if(migration_in_progress) {
          migration_in_progress = false;
          if (!dump_jvm()) {
            fprintf(log, "ERROR: JVM dump failed.\n");
          }
        }
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
    if (pthread_mutex_init(&mutex, NULL) != 0)
    {
        fprintf(log, "\n mutex init failed\n");
        return -11;
    }
    pthread_mutex_lock(&mutex);
    
    if((log = fopen("agent.log", "a")) == NULL) {
        fprintf(stderr, "ERROR: Unable to open log file.\n");
    }
    
    if(signal(SIGUSR2, signalHandler) == SIG_ERR) {
        fprintf(stderr, "ERROR: Could not place signal handler for SIGUSR2.\n");
    }
    
    return 0;
}

/* Agent_OnUnload() is called last */
JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *vm)
{
}
