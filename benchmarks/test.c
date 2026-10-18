#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "../thread-worker.h"

/* A scratch program template on which to call and
 * test thread-worker library functions as you implement
 * them.
 *
 * You can modify and use this program as much as possible.
 * This will not be graded.
 */

#define NTHREADS 4
#define ITERS 100000

pthread_mutex_t mutex;
int counter = 0;

/* test 1: yield interleaving + return value through join */
void *yielder(void *arg) {
	int id = *(int *)arg;
	for (int i = 0; i < 3; i++) {
		printf("thread %d step %d\n", id, i);
		worker_yield();
	}
	return (void *)(long)(id * 10);
}

/* test 2: mutex protects a shared counter under timer preemption */
void *incrementer(void *arg) {
	for (int i = 0; i < ITERS; i++) {
		pthread_mutex_lock(&mutex);
		counter++;
		pthread_mutex_unlock(&mutex);
	}
	pthread_exit(NULL);
}

int main(int argc, char **argv) {
	pthread_t t[NTHREADS];
	int ids[NTHREADS];

	for (int i = 0; i < NTHREADS; i++) {
		ids[i] = i;
		pthread_create(&t[i], NULL, yielder, &ids[i]);
	}
	for (int i = 0; i < NTHREADS; i++) {
		void *ret;
		pthread_join(t[i], &ret);
		printf("joined thread %d, returned %ld (expected %d)\n", i, (long)ret, i * 10);
	}

	pthread_mutex_init(&mutex, NULL);
	for (int i = 0; i < NTHREADS; i++)
		pthread_create(&t[i], NULL, incrementer, NULL);
	for (int i = 0; i < NTHREADS; i++)
		pthread_join(t[i], NULL);
	pthread_mutex_destroy(&mutex);

	printf("counter = %d (expected %d) %s\n", counter, NTHREADS * ITERS,
	       counter == NTHREADS * ITERS ? "PASS" : "FAIL");
	print_app_stats();
	return 0;
}
