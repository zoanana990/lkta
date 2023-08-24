#pragma once

#if USE_LINUX

#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Atomically check if '*futex == value', and if so, go to sleep */
static inline void futex_wait(atomic int *futex, int value)
{
    syscall(SYS_futex, futex, FUTEX_WAIT_PRIVATE, value, NULL);
}

/* Wake up 'limit' threads currently waiting on 'futex' */
static inline void futex_wake(atomic int *futex, int limit)
{
    syscall(SYS_futex, futex, FUTEX_WAKE_PRIVATE, limit);
}

/* Wake up 'limit' waiters, and re-queue the rest onto a different futex */
static inline void futex_requeue(atomic int *futex,
                                 int limit,
                                 atomic int *other)
{
    syscall(SYS_futex, futex, FUTEX_REQUEUE_PRIVATE, limit, INT_MAX, other);
}

/* Returns 0 if the futex was successfully locked. */
static inline int futex_trylock_pi(atomic int *futex)
{
    return syscall(SYS_futex, futex, FUTEX_TRYLOCK_PI);
}

/* Returns 0 if the futex was successfully locked. */
static inline int  futex_lock_pi(atomic int *futex)
{
    return syscall(SYS_futex, futex, FUTEX_LOCK_PI);
}

/* Returns 0 if the futex was successfully unlocked. */
static inline int  futex_unlock_pi(atomic int *futex)
{
    return syscall(SYS_futex, futex, FUTEX_UNLOCK_PI);
}
#endif