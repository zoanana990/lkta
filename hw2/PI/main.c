#include <stdio.h>
#include <pthread.h>
#include "mutex.h"

mutex_t common_lock;

enum {
    PRIORITY_LOW = 0,
    PRIORITY_MED = 1,
    PRIORITY_HIGH = 2
};

void task1()
{
    mutex_lock_pi(&common_lock);
    printf("Low Priority Task is running\n");
    mutex_unlock_pi(&common_lock);
}

void task2()
{
    /* try to access the lock, cannot execute first */
    mutex_lock_pi(&common_lock);
    printf("Med Priority Task is running\n");
    mutex_unlock_pi(&common_lock);
}

void task3()
{
    printf("r\nm");
    mutex_lock_pi(&common_lock);
    printf("High Priority Task is running\n");
    mutex_unlock_pi(&common_lock);
}

static void (*TASKS[])() = {task1, task2, task3};

#define THREAD_N 3

int main()
{
    pthread_t          thread[THREAD_N];
    pthread_attr_t     attr;
    struct sched_param param;            /* scheduler parameter */
    int                ret;              /* result */

    printf("start\n");
    /* mutex init */
    mutex_init(&common_lock);

    /* pthread attribute init */
    ret = pthread_attr_init (&attr);
    if(ret)
    {
        printf("pthread attribute initialized failed\n");
        goto error;
    }

    /* set pthread scheduler policy, FIFO is the preemptive scheduler */
    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    if(ret)
    {
        printf("pthread attribute set scheduler policy failed\n");
        goto error;
    }

    /* set the inheritance, not use the parent thread scheduler */
    ret = pthread_attr_setinheritsched (&attr, PTHREAD_EXPLICIT_SCHED);
    if (ret)
    {
        printf("pthread attribute set scheduler policy failed\n");
        goto error;
    }

    for(int i=0; i<THREAD_N; i++)
    {
        int priority = (i % THREAD_N) * 10 + 10;
        param.sched_priority = priority;
        pthread_attr_setschedparam (&attr, &param);
        
        /* Some pthread attribute allocate */
        pthread_create(&thread[i], &attr, (void *)TASKS[i], NULL);
    }

    return 0;
error:
    printf("Priroity Inheritance test failed\n");
    return 1;
}