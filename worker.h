#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

// defined timeQUANTUM to 12 ms,

#define QUANTUMTIME 12


#define READY 0
#define SCHEDULED 1
#define BLOCKED 2

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>


typedef uint worker_t;

typedef struct TCB{
	
	// thread Id
	worker_t threadId;

	// thread context
	ucontext_t cctx;

	// thread status
	int status;
	
	//thread priority
	int priority;

	//thread counter
	//clock_t threadTime;

} tcb; 


typedef struct worker{

	tcb * threadControlBlock;

} worker;

/* mutex struct definition */
typedef struct worker_mutex_t {
	
	//flag is used to indicate whether mutex is available or nor
	int flag;

	//pointer to the thread holding the mutex
	worker * thrd;
} worker_mutex_t;

typedef struct node{

	worker * thr;
	struct node * next;


}node;


typedef struct runqueue{

	//head
	node * head;
	//tail
	node * tail;

	

} runqueue;


/* Function Declarations: */

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif
