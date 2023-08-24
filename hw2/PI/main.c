#include <stdio.h>
#include <pthread.h>
#include "../mutex.h"

mutex_t common_lock;
int     common_resource = 0;

enum {
    PRIORITY_LOW = 0,
    PRIORITY_MED = 1,
    PRIORITY_HIGH = 2
};

void* task1()
{
    mutex_lock_pi(&common_lock);
    printf("Low Priority Task is running\n");
    common_resource = 1;
    printf("low priority tid = %d\n", common_resource);
    mutex_unlock_pi(&common_lock);
    return NULL;
}

void* task2()
{
    /* try to access the lock, cannot execute first */
    mutex_lock_pi(&common_lock);
    printf("Med Priority Task is running\n");
    common_resource = 2;
    printf("medium priority tid = %d\n", common_resource);
    mutex_unlock_pi(&common_lock);
    return NULL;
}

void* task3()
{
    mutex_lock_pi(&common_lock);
    printf("High Priority Task is running\n");
    common_resource = 3;
    printf("high priority tid = %d\n", common_resource);
    mutex_unlock_pi(&common_lock);
    return NULL;
}

static void *(*TASKS[])() = {task1, task2, task3};

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
        int priority = (i % THREAD_N) + 10;
        param.sched_priority = priority;
        pthread_attr_setschedparam (&attr, &param);
        
        /* Some pthread attribute allocate */
        pthread_create(&thread[i], &attr, (void *)TASKS[i], NULL);
    }
    // pthread_create(&thread[0], &attr, (void *)task1, NULL);

    for (int i = 0; i < THREAD_N; ++i) {
        if (pthread_join(thread[i], NULL) != 0)
            goto error;
    }
    return 0;
error:
    printf("Priroity Inheritance test failed\n");
    return 1;
}