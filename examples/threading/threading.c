#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
	
	thread_func_args->thread_complete_success = false;
	usleep(thread_func_args->wait_to_obtain_ms*1000);
	
	if (pthread_mutex_lock(thread_func_args->mutex) != 0) {
		ERROR_LOG("Failed to retrieve mutex");
		return thread_func_args;
	}

	usleep(thread_func_args->wait_to_release_ms*1000);
	
	if (pthread_mutex_unlock(thread_func_args->mutex) != 0) {
		ERROR_LOG("Failed to release mutex");
		return thread_func_args;
	}

	thread_func_args->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    struct thread_data *args = malloc(sizeof(struct thread_data));

	if (args != NULL) {
		args->mutex = mutex;
		args->wait_to_obtain_ms=wait_to_obtain_ms;
		args->wait_to_release_ms=wait_to_release_ms;
		args->thread_complete_success=false;
		
		int pth = pthread_create(thread, NULL, threadfunc, args);
		
		if (pth != 0) {
			ERROR_LOG("Failed to create thread");
			free(args);
			return false;
		}
		return true;
	}
	ERROR_LOG("Failed to allocate for arguments");
    return false;
}

