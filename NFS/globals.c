#include <stdio.h>
#include <pthread.h>
int global_flag = -1;
int async_write_active=0;
int path_async_number = -1;
int error_flag=0;
pthread_mutex_t flag_mutex = PTHREAD_MUTEX_INITIALIZER;