// File:	worker_t.h

// List all group member's name: Yusuf Aminu(yta12), Kuthab Ibrahim(ki91)
// username of iLab:butter
// iLab Server:butter.cs.rutgers.edu

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1
#define READY 0
#define SCHEDULED 1
#define BLOCKED 2
#define TERMINATED 3
#define STACK_SIZE (64 * 1024)
#define QUANTUM 10 
#define MLFQ_LEVELS 4 
#ifndef MLFQ_S
#define MLFQ_S (100 * QUANTUM) // MLFQ Rule 5: every S milliseconds, all threads move back to the top queue
#endif
/* include lib header files that you need here: */
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint worker_t;

typedef struct TCB {

	worker_t Id;
	int status;
	ucontext_t context;
	void* stack;
	void* return_val;
	struct TCB* joiner;
	struct TCB* next;
	void *(*function)(void*);
	void* arg;

	int elapsed;
	int priority;
	long level_used;
	long cntx_switches;
	struct timespec arrival;
	int has_run;
	double resp_time;
	double turn_time;
} tcb;

typedef struct queue {
	tcb *head;
	tcb *tail;
} queue;

/* mutex struct definition */
typedef struct worker_mutex_t {
	/* add something here */
	int lock_flag;
	tcb *owner;
	queue waiters;
	// YOUR CODE HERE
} worker_mutex_t;

/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

// YOUR CODE HERE


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


/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

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
