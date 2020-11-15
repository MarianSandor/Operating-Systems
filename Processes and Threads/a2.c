#include <stdio.h>
#include <stdlib.h>
#include <time.h> 

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "a2_helper.h"

#define P2_NO_THREADS 5
#define P3_NO_THREADS 5
#define P7_NO_THREADS 41

int WAIT = 1, FREE_ME = 0, P7T10_IN = 0;
pthread_mutex_t lock;
pthread_cond_t wait_for_T7_10;
pthread_cond_t wait_for_threads;
int no_threads = 0;

void P(int semId, int semNr, int value)
{
	struct sembuf op = {semNr, -value, 0};
    semop(semId, &op, 1);
}

void V(int semId, int semNr, int value)
{
    struct sembuf op = {semNr, value, 0};
    semop(semId, &op, 1);
}

void Wait_For_Zero(int semId, int semNr) 
{
	struct sembuf op = {semNr, 0, 0};
    semop(semId, &op, 1);
}

void* P3_thread(void *arg) 			// P3_thread uses semNr: 0, 1, 3, 4
{		
	int id = ((int *) arg)[0];
	int semId = ((int *) arg)[1];

	if (id == 1) {		// identifying T3.1
		P(semId, 0, 1);		// wait for T3.5 to start
	}

	if (id == 3) {		// identifying T3.3
		P(semId, 3, 1);		// wait for T2.5 to terminate
	}

	info(BEGIN, 3, id);

	if (id == 5) {		// identifying T3.5
		V(semId, 0, 1);		// give permission to T3.1 to start
		P(semId, 1, 1); 		// wait for T3.1 to terminate
	}

	info(END, 3, id);

	if (id == 1) {		// identifying T3.1
		V(semId, 1, 1);		// give permission to T3.5 to terminate
	}

	if (id == 3) {		// identifying T3.3
		V(semId, 4, 1);		// give permission to T2.2 to start
	}

	return NULL;
}

void* P2_thread(void *arg) 			// P3_thread uses semNr: 3, 4
{		
	int id = ((int *) arg)[0];
	int semId = ((int *) arg)[1];

	if (id == 2) {		// identifying T2.2
		P(semId, 4, 1);		// wait for T3.3 to terminate
	}

	info(BEGIN, 2, id);

	info(END, 2, id);

	if (id == 5) {		// identifying T2.5
		V(semId, 3, 1);		// give permission to T3.3 to terminate
	}

	return NULL;
}

void* P7_thread(void *arg) 		// P7_thread uses semNr: 2
{		
	int id = ((int *) arg)[0];
	int semId = ((int *) arg)[1];

	if (id != 10) {											// identify any thread that is not P7T10 and if P7T10 is not 
		pthread_mutex_lock(&lock);							// in the critical region waiting then block the current thread
		while (P7T10_IN == 0) {
			pthread_cond_wait(&wait_for_T7_10, &lock);
		}
		pthread_mutex_unlock(&lock);
	}

	P(semId, 2, 1);		// thread asks for permission to begin

	info(BEGIN, 7, id);

	if (id == 10) {						// identifying T7.10
		pthread_mutex_lock(&lock);
			while (no_threads < 5) {
				FREE_ME = 1;
				P7T10_IN = 1;										
				pthread_cond_broadcast(&wait_for_T7_10);			// T7.10 signals that it entered the critical region
				pthread_cond_wait(&wait_for_threads, &lock);		// T7.10 must wait for other 5 threads to enter the critical region
			}
		pthread_mutex_unlock(&lock);
	}
	else if (WAIT == 1) {				// if WAIT = 1 it means T7.10 is waiting for threads to enter the critical region 
		pthread_mutex_lock(&lock);
			no_threads++;
			if (no_threads == 5 && FREE_ME == 1) {			// identifies the fifth thread to wait for T7.10 and checks if it needs
				pthread_cond_signal(&wait_for_threads);		// to signal T7.10
			}
			
			if (no_threads < 6) {		// if less than 6 threads are in the critical region then the current thread is blocked
				while (WAIT == 1) {		
					pthread_cond_wait(&wait_for_T7_10, &lock);  // threads wait for T7.10 to finish
				}
			}
			no_threads--;
		pthread_mutex_unlock(&lock);
	}

	info(END, 7, id);

	if (id == 10) {						// identifying T7.10
		pthread_mutex_lock(&lock);
		WAIT = 0;
		FREE_ME = 0;
		pthread_cond_broadcast(&wait_for_T7_10);	// release the waiting threads
		pthread_mutex_unlock(&lock);
	}

	V(semId, 2, 1);		// thread retrieves permission

	return NULL;
}

void P2(pthread_t P2_thread_id[P2_NO_THREADS], int P2_arg[P2_NO_THREADS][2], int proc_semId) 
{
	for (int i = 0; i < P2_NO_THREADS; i++) {
    	P2_arg[i][0] = i + 1;
    	P2_arg[i][1] = proc_semId;

    	if (pthread_create(&P2_thread_id[i], NULL, P2_thread, &P2_arg[i]) != 0) {
			perror("Error creating a new thread\n");
			exit(2);
		}
    }

    for (int i = 0; i < P2_NO_THREADS; i++) {
		pthread_join(P2_thread_id[i], NULL);
	}
}

void P3(pthread_t P3_thread_id[P3_NO_THREADS], int P3_arg[P3_NO_THREADS][2], int proc_semId) 
{
	for (int i = 0; i < P3_NO_THREADS; i++) {
    	P3_arg[i][0] = i + 1;
    	P3_arg[i][1] = proc_semId;

    	if (pthread_create(&P3_thread_id[i], NULL, P3_thread, &P3_arg[i]) != 0) {
			perror("Error creating a new thread\n");
			exit(2);
		}
   }

    for (int i = 0; i < P3_NO_THREADS; i++) {
		pthread_join(P3_thread_id[i], NULL);
	}
}

void P7(pthread_t P7_thread_id[P7_NO_THREADS], int P7_arg[P7_NO_THREADS][2], int proc_semId) 
{
	if (pthread_mutex_init(&lock, NULL) != 0) {
		perror("Cannot initialize the lock");
		exit(3);
	}
						
	if (pthread_cond_init(&wait_for_T7_10, NULL) != 0) {
		perror("Cannot initialize the condition variable");
		exit(4);
	}

	if (pthread_cond_init(&wait_for_threads, NULL) != 0) {
		perror("Cannot initialize the condition variable");
		exit(4);
	}

	semctl(proc_semId, 2, SETVAL, 6);			// set value of sem[2] = 6 ---> allow 6 threads concurrently

	for (int i = 0; i < P7_NO_THREADS; i++) {
    	P7_arg[i][0] = i + 1;
    	P7_arg[i][1] = proc_semId;

    	if (pthread_create(&P7_thread_id[i], NULL, P7_thread, &P7_arg[i]) != 0) {
			perror("Error creating a new thread\n");
			exit(2);
		}
    }

    for (int i = 0; i < P7_NO_THREADS; i++) {
		pthread_join(P7_thread_id[i], NULL);
	} 

	if (pthread_mutex_destroy(&lock) != 0) {
    	perror("Cannot destroy the lock");
    	exit(5);
    }

    if (pthread_cond_destroy(&wait_for_T7_10) != 0) {
    	perror("Cannot destroy the condition variable");
    	exit(6);
    }

    if (pthread_cond_destroy(&wait_for_threads) != 0) {
    	perror("Cannot destroy the condition variable");
    	exit(6);
    }
}

int main()
{
	int pid[6];

	pthread_t P2_thread_id[P2_NO_THREADS];
	int P2_arg[P2_NO_THREADS][2];		// function argument for P2_thread: [0] --> thread_id; [1] --> sem_id

	pthread_t P3_thread_id[P3_NO_THREADS];
	int P3_arg[P3_NO_THREADS][2];		// function argument for P3_thread: [0] --> thread_id; [1] --> sem_id

	pthread_t P7_thread_id[P7_NO_THREADS];
	int P7_arg[P7_NO_THREADS][2];		// function argument for P7_thread: [0] --> thread_id; [1] --> sem_id

	int proc_semId = semget(IPC_PRIVATE, 5, IPC_CREAT | 0600);

	if (proc_semId < 0) { 
		perror("Error creating process semaphores"); 
		exit(1); 
	}

	int init_values[5] = {0, 0, 0, 0, 0};					//sem[0]-->0, sem[1]-->0, sem[2]-->0, sem[3]-->0, sem[4]-->0, 
	semctl(proc_semId, 0, SETALL, &init_values);			// initialize all semaphores

    init();

    info(BEGIN, 1, 0);

    pid[0] = fork();

    if (pid[0] == 0) {			// process P2 block
    	info(BEGIN, 2, 0);

    	pid[1] = fork();

    	if (pid[1] == 0) {			// process P3 block
    		info(BEGIN, 3, 0);

    		pid[3] = fork();

    		if (pid[3] == 0) {		// process P5 block
    			info(BEGIN, 5, 0);  

    			info(END, 5, 0);
    		}
    		else {

    			pid[5] = fork();

    			if (pid[5] == 0) {		//process P7 block
	     			info(BEGIN, 7, 0); 

	     			P7(P7_thread_id, P7_arg, proc_semId);

	    			info(END, 7, 0);  				
    			}
    			else {
    				P3(P3_thread_id, P3_arg, proc_semId);

    				waitpid(pid[3], 0, 0);	// P3 wait for process P5
    				waitpid(pid[5], 0, 0);	// P3 wait for process P7

    				info(END, 3, 0);
    			}
    		}
    	}
    	else {
    		P2(P2_thread_id, P2_arg, proc_semId);

    		waitpid(pid[1], 0, 0);		// P2 wait for process P3

    		info(END, 2, 0);
    	}
    }
    else {	// process P1 block					
    	pid[2] = fork();

    	if (pid[2] == 0) {		//process P4 block
    		info(BEGIN, 4, 0);

    		pid[4] = fork();

    		if (pid[4] == 0) {		//process P6 block;
    			info(BEGIN, 6, 0);  

    			info(END, 6, 0);
    		}
    		else {
    			waitpid(pid[4], 0, 0);	//P4 wait for process P6
    			info(END, 4, 0);
    		}
    	}
    	else {
    		waitpid(pid[0], 0, 0);	// P1 wait for process P2
    		waitpid(pid[2], 0, 0);	// P1 wait for process P4

    		semctl(proc_semId, 0, IPC_RMID, 0);
    		
    		info(END, 1, 0);
    	}
    }

    return 0;
}
