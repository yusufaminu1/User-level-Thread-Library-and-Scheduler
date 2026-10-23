// File:	thread-worker.c

// List all group member's name: Yusuf Aminu(yta12), Kuthab Ibrahim(ki91)
// username of iLab: butter
// iLab Server: butter.cs.rutgers.edu

#include "thread-worker.h"

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;


// INITAILIZE ALL YOUR OTHER VARIABLES HERE
// YOUR CODE HERE
static queue runqueue;            
static queue mlfq[MLFQ_LEVELS];   
static tcb *current;
static tcb *main_thread;
static ucontext_t scheduler_ctx;
static worker_t next_id;
static int intialized = 0;
static tcb **all_threads;   
static int all_threads_cap;
static long slice_start;    
static long last_boost;     
static int boost_cut;       
static long finished_threads;   
static double total_turn_time; 
static double total_resp_time;  
static void schedule();
static tcb *sched_psjf();
static tcb *sched_mlfq();


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


static void queue_remove(queue *q, tcb *prev, tcb *t){
	if(prev == NULL){
		q->head = t->next;
	}else{
		prev->next = t->next;
	}
	if(q->tail == t){
		q->tail = prev;
	}
	t->next = NULL;
}


static void make_ready(tcb *t){
	t->status = READY;
#ifndef MLFQ
	enqueue(&runqueue, t);
#else
	enqueue(&mlfq[t->priority], t);
#endif
}


static double ms_between(const struct timespec *start, const struct timespec *end){
	return (end->tv_sec - start->tv_sec) * 1000.0 + (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

static long cpu_time_us(){
	struct timespec ts;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

static long time_slice(tcb *t){
#ifndef MLFQ
	return (long)(QUANTUM * 1000L);
#else
	return (long)(QUANTUM * 1000L) << t->priority;
#endif
}

static void start_slice(tcb *t){
	long usec = time_slice(t);
#ifdef MLFQ
	slice_start = cpu_time_us();
	usec -= t->level_used;   
	long until_boost = last_boost + (long)(MLFQ_S * 1000L) - slice_start;
	boost_cut = until_boost < usec;
	if(boost_cut){
		usec = until_boost > 0 ? until_boost : 1;
	}
#endif
	struct itimerval timer;
	memset(&timer, 0, sizeof(timer));
	setitimer(ITIMER_PROF, &timer, NULL);
	sigset_t pending;
	sigpending(&pending);
	if(sigismember(&pending, SIGPROF)){
		sigset_t prof;
		int sig;
		sigemptyset(&prof);
		sigaddset(&prof, SIGPROF);
		sigwait(&prof, &sig);
	}

	timer.it_value.tv_sec = usec / 1000000;
	timer.it_value.tv_usec = usec % 1000000;
	setitimer(ITIMER_PROF, &timer, NULL);
}


static void timer_handler(int signum){
	swapcontext(&current->context,&scheduler_ctx);
}


static void init_library(){
	getcontext(&scheduler_ctx);
	scheduler_ctx.uc_stack.ss_sp = malloc(STACK_SIZE);
	scheduler_ctx.uc_stack.ss_size = STACK_SIZE;
	scheduler_ctx.uc_stack.ss_flags = 0;
	scheduler_ctx.uc_link = NULL;
	
	sigaddset(&scheduler_ctx.uc_sigmask, SIGPROF);
	makecontext(&scheduler_ctx, schedule, 0);

	
	main_thread = calloc(1, sizeof(tcb));
	main_thread->Id = next_id++;
	main_thread->status = SCHEDULED;
	main_thread->stack = NULL;
	main_thread->has_run = 1;  
	current = main_thread;
	register_thread(main_thread);

	
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = timer_handler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGPROF, &sa, NULL);

#ifdef MLFQ
	last_boost = cpu_time_us();
#endif
	start_slice(main_thread);

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
	t->priority = 0;  

	t->stack = malloc(STACK_SIZE);
	getcontext(&t->context);
	t->context.uc_stack.ss_sp = t->stack;
	t->context.uc_stack.ss_size = STACK_SIZE;
	t->context.uc_stack.ss_flags = 0;
	t->context.uc_link = NULL;
	sigdelset(&t->context.uc_sigmask, SIGPROF);
	makecontext(&t->context, thread_start, 0);

	register_thread(t);
	clock_gettime(CLOCK_MONOTONIC, &t->arrival);
	make_ready(t);

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

	// turnaround: from first entering the runqueue until now; fold this thread into the running averages
	if(current != main_thread){
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		current->turn_time = ms_between(&current->arrival, &now);
		finished_threads++;
		total_turn_time += current->turn_time;
		total_resp_time += current->resp_time;
		avg_turn_time = total_turn_time / finished_threads;
		avg_resp_time = total_resp_time / finished_threads;
	}

	
	if(current->joiner != NULL){
		make_ready(current->joiner);
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

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex,
                          const pthread_mutexattr_t *mutexattr) {
	mutex->lock_flag = 0;
	mutex->owner = NULL;
	mutex->waiters.head = NULL;
	mutex->waiters.tail = NULL;
	return 0;
};

/* aquire the mutex lock */
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
		make_ready(t);
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
		tcb *prev = current;
		tcb *next;

#ifndef MLFQ
		// Choose PSJF
		next = sched_psjf();
#else
		// Choose MLFQ
		next = sched_mlfq();
#endif

		if(next == NULL){
			exit(main_thread->status == TERMINATED ? 0 : 1);
		}

		if(next != prev){
			tot_cntx_switches++;
			next->cntx_switches++;
		}
		if(!next->has_run){
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			next->resp_time = ms_between(&next->arrival, &now);
			next->has_run = 1;
		}

		next->status = SCHEDULED;
		current = next;
		start_slice(next);
		swapcontext(&scheduler_ctx, &next->context);
	}
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static tcb *sched_psjf() {
	if(current->status == SCHEDULED){
		current->elapsed++;
	}
	
	if(current->status == SCHEDULED || current->status == READY){
		make_ready(current);
	}

	
	tcb *best = NULL;
	tcb *best_prev = NULL;
	tcb *prev = NULL;
	for(tcb *t = runqueue.head; t != NULL; t = t->next){
		if(best == NULL || t->elapsed < best->elapsed){
			best = t;
			best_prev = prev;
		}
		prev = t;
	}
	if(best != NULL){
		queue_remove(&runqueue, best_prev, best);
	}
	return best;
}



static void priority_boost(){
	for(worker_t id = 0; id < next_id; id++){
		tcb *t = all_threads[id];
		if(t != NULL && t->status != TERMINATED){
			t->priority = 0;
			t->level_used = 0;
		}
	}
	for(int level = 1; level < MLFQ_LEVELS; level++){
		queue *q = &mlfq[level];
		if(q->head == NULL){
			continue;
		}
		if(mlfq[0].tail == NULL){
			mlfq[0].head = q->head;
		}else{
			mlfq[0].tail->next = q->head;
		}
		mlfq[0].tail = q->tail;
		q->head = NULL;
		q->tail = NULL;
	}
}

/* Preemptive MLFQ scheduling algorithm */
static tcb *sched_mlfq() {
	long now = cpu_time_us();

	
	if(current->status != TERMINATED){
		long slice = time_slice(current);
		if(current->status == SCHEDULED && !boost_cut){
			current->level_used = slice;               
		}else{
			current->level_used += now - slice_start;
		}
		if(current->level_used >= slice){
			if(current->priority < MLFQ_LEVELS - 1){
				current->priority++;
			}
			current->level_used = 0;
		}
	}
	if(current->status == SCHEDULED || current->status == READY){
		make_ready(current);
	}

	if(now - last_boost >= (long)(MLFQ_S * 1000L)){
		priority_boost();
		last_boost = now;
	}

	for(int level = 0; level < MLFQ_LEVELS; level++){
		if(mlfq[level].head != NULL){
			return dequeue(&mlfq[level]);
		}
	}
	return NULL;
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

