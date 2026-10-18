// File:	thread-worker.c

// List all group member's name: Yusuf Aminu, Kuthab Ibrahim
// username of iLab: butter
// iLab Server:

#include "thread-worker.h"

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;


// INITAILIZE ALL YOUR OTHER VARIABLES HERE
// YOUR CODE HERE
static queue runqueue;
static tcb *current;
static ucontext_t scheduler_ctx;
static worker_t next_id;
static int intialized = 0;
static tcb **all_threads;   // all_threads[id] -> TCB, so join can find a thread by id
static int all_threads_cap;
static void schedule();


static void block_timer(){
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGPROF);
	sigprocmask(SIG_BLOCK, &set, NULL);
}

static void unblock_timer(){
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGPROF);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}


static void register_thread(tcb *t){
	if(t->Id >= all_threads_cap){
		int new_cap = all_threads_cap ? all_threads_cap * 2 : 64;
		while(new_cap <= t->Id){
			new_cap *= 2;
		}
		all_threads = realloc(all_threads, new_cap * sizeof(tcb *));
		memset(all_threads + all_threads_cap, 0, (new_cap - all_threads_cap) * sizeof(tcb *));
		all_threads_cap = new_cap;
	}
	all_threads[t->Id] = t;
}

static void enqueue(queue *q, tcb *t){
	t->next = NULL;
	if(q->tail == NULL){
		q->head = t;
		q->tail = t;
	}else{
		q->tail->next = t;
		q->tail = t;
	}
	
	
}
static tcb *dequeue(queue *q){
	if(q->head == NULL){
		return NULL;
	}
	tcb *old_head = q->head;
	q->head = q->head->next;
	if(q->head == NULL){
		q->tail = NULL;
	}
	old_head->next = NULL;
	return old_head;
}


static void timer_handler(int signum){
	swapcontext(&current->context,&scheduler_ctx);
}


static void init_library(){
	// scheduler context with its own stack
	getcontext(&scheduler_ctx);
	scheduler_ctx.uc_stack.ss_sp = malloc(STACK_SIZE);
	scheduler_ctx.uc_stack.ss_size = STACK_SIZE;
	scheduler_ctx.uc_stack.ss_flags = 0;
	scheduler_ctx.uc_link = NULL;
	
	sigaddset(&scheduler_ctx.uc_sigmask, SIGPROF);
	makecontext(&scheduler_ctx, schedule, 0);

	
	tcb *main_tcb = calloc(1, sizeof(tcb));
	main_tcb->Id = next_id++;
	main_tcb->status = SCHEDULED;
	main_tcb->stack = NULL; 
	current = main_tcb;
	register_thread(main_tcb);

	
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = timer_handler;
	sigaction(SIGPROF, &sa, NULL);

	
	struct itimerval timer;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = QUANTUM * 1000;
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = QUANTUM * 1000;
	setitimer(ITIMER_PROF, &timer, NULL);

	intialized = 1;
}


static void thread_start(){
	void *ret =current->function(current->arg);
	worker_exit(ret);
}

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr,
                      void *(*function)(void*), void * arg) {
	block_timer();
	if(!intialized){
		init_library();
	}

	tcb *t = calloc(1, sizeof(tcb));
	t->Id = next_id++;
	*thread = t->Id;
	t->function = function;
	t->arg = arg;

	t->stack = malloc(STACK_SIZE);
	getcontext(&t->context);
	t->context.uc_stack.ss_sp = t->stack;
	t->context.uc_stack.ss_size = STACK_SIZE;
	t->context.uc_stack.ss_flags = 0;
	t->context.uc_link = NULL;
	sigdelset(&t->context.uc_sigmask, SIGPROF);
	makecontext(&t->context, thread_start, 0);

	t->status = READY;
	register_thread(t);
	enqueue(&runqueue, t);

	unblock_timer();
	return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	block_timer();
	current->status = READY;
	swapcontext(&current->context, &scheduler_ctx);
	unblock_timer();
	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
	block_timer();
	current->return_val = value_ptr;
	current->status = TERMINATED;

	
	if(current->joiner != NULL){
		current->joiner->status = READY;
		enqueue(&runqueue, current->joiner);
	}

	
	setcontext(&scheduler_ctx);
};


/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	block_timer();
	if(thread >= all_threads_cap || all_threads[thread] == NULL){
		unblock_timer();
		return -1;
	}
	tcb *target = all_threads[thread];

	
	if(target->status != TERMINATED){
		target->joiner = current;
		current->status = BLOCKED;
		swapcontext(&current->context, &scheduler_ctx);
	}

	if(value_ptr != NULL){
		*value_ptr = target->return_val;
	}
	all_threads[thread] = NULL;
	free(target->stack);
	free(target);

	unblock_timer();
	return 0;
};


int worker_mutex_init(worker_mutex_t *mutex,
                          const pthread_mutexattr_t *mutexattr) {
	mutex->lock_flag = 0;
	mutex->owner = NULL;
	mutex->waiters.head = NULL;
	mutex->waiters.tail = NULL;
	return 0;
};


int worker_mutex_lock(worker_mutex_t *mutex) {
	block_timer();

	while(__sync_lock_test_and_set(&mutex->lock_flag, 1)){
		current->status = BLOCKED;
		enqueue(&mutex->waiters, current);
		swapcontext(&current->context, &scheduler_ctx);
	}
	mutex->owner = current;
	unblock_timer();
	return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	block_timer();
	mutex->owner = NULL;
	__sync_lock_release(&mutex->lock_flag);

	
	tcb *t;
	while((t = dequeue(&mutex->waiters)) != NULL){
		t->status = READY;
		enqueue(&runqueue, t);
	}
	unblock_timer();
	return 0;
};


/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	
	if(mutex->lock_flag){
		return -1;
	}
	mutex->owner = NULL;
	mutex->waiters.head = NULL;
	mutex->waiters.tail = NULL;
	return 0;
};

/* scheduler: runs on its own context with SIGPROF blocked */
static void schedule() {
	while(1){
		if(current->status == SCHEDULED || current->status == READY){
			current->status = READY;
			enqueue(&runqueue, current);
		}

		// TEMPORARY round-robin until Part 2 replaces this with sched_psjf / sched_mlfq
#ifndef MLFQ
		// Choose PSJF
#else
		// Choose MLFQ
#endif
		tcb *next = dequeue(&runqueue);
		if(next == NULL){
			fprintf(stderr, "deadlock: no runnable threads\n");
			exit(1);
		}

		next->status = SCHEDULED;
		current = next;
		tot_cntx_switches++;
		swapcontext(&scheduler_ctx, &next->context);
	}
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}


/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}

//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {

       fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
       fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
       fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}


// Feel free to add any other functions you need

// YOUR CODE HERE

