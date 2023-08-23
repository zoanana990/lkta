#include <assert.h>
#include <errno.h>
#include <err.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#define verify(x)                                                      \
    do {                                                               \
        int e;                                                         \
        if ((e = x) != 0) {                                            \
            fprintf(stderr, "%s(%d) %s: %s\n", __FILE__, __LINE__, #x, \
                    strerror(e));                                      \
            exit(1);                                                   \
        }                                                              \
    } while (0)

typedef int cmp_t(const void *a, const void *b);
typedef int (*cmp_func_t)(const void *a, const void *b);
typedef void (*swap_func_t)(void *a, void *b, int size);
typedef int (*cmp_r_func_t)(const void *a, const void *b, const void *priv);
static inline char *med3(char *, char *, char *, cmp_t *, void *);
static inline void swapfunc(char *, char *, int, int);

#define min(a, b)           \
    ({                      \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a < _b ? _a : _b;  \
    })

/* Qsort routine from Bentley & McIlroy's "Engineering a Sort Function" */
#define swapcode(TYPE, parmi, parmj, n) \
    {                                   \
        long i = (n) / sizeof(TYPE);    \
        TYPE *pi = (TYPE *) (parmi);    \
        TYPE *pj = (TYPE *) (parmj);    \
        do {                            \
            TYPE t = *pi;               \
            *pi++ = *pj;                \
            *pj++ = t;                  \
        } while (--i > 0);              \
    }

static inline void swapfunc(char *a, char *b, int n, int swaptype)
{
    if (swaptype <= 1)
    swapcode(long, a, b, n) else swapcode(char, a, b, n)
}

#define swap(a, b)                         \
    do {                                   \
        if (swaptype == 0) {               \
            long t = *(long *) (a);        \
            *(long *) (a) = *(long *) (b); \
            *(long *) (b) = t;             \
        } else                             \
            swapfunc(a, b, es, swaptype);  \
    } while (0)

#define vecswap(a, b, n)                 \
    do {                                 \
        if ((n) > 0)                     \
            swapfunc(a, b, n, swaptype); \
    } while (0)

#define CMP(t, x, y) (cmp((x), (y)))

static inline char *med3(char *a, char *b, char *c, cmp_t *cmp, void *thunk)
{
    return CMP(thunk, a, b) < 0
           ? (CMP(thunk, b, c) < 0 ? b : (CMP(thunk, a, c) < 0 ? c : a))
           : (CMP(thunk, b, c) > 0 ? b : (CMP(thunk, a, c) < 0 ? a : c));
}

/* Condition of starting a new thread. */
enum thread_state {
    ts_idle, /* Idle, waiting for instructions. */
    ts_work, /* Has work to do. */
    ts_term  /* Asked to terminate. */
};

/* Variant part passed to qsort invocations. */
struct isort {
    enum thread_state st;   /* For coordinating work. */
    struct common *common;  /* Common shared elements. */
    void *a;                /* Array base. */
    size_t n;               /* Number of elements. */
    int depth;              /* the depth limit in this array */
    pthread_t id;           /* Thread id. */
    pthread_mutex_t mtx_st; /* For signalling state change. */
    pthread_cond_t cond_st; /* For signalling state change. */
};

/* Invariant common part, shared across invocations. */
struct common {
    int swaptype;           /* Code to use for swapping */
    size_t es;              /* Element size. */
    void *thunk;            /* Thunk for qsort_r */
    cmp_t *cmp;             /* Comparison function */
    int nthreads;           /* Total number of pool threads. */
    int idlethreads;        /* Number of idle threads in pool. */
    int forkelem;           /* Minimum number of elements for a new thread. */
    struct isort *pool;     /* Fixed pool of threads. */
    pthread_mutex_t mtx_al; /* For allocating threads in the pool. */
};

static void *isort_thread(void *p);

static bool is_aligned(const void *base, size_t size, unsigned char align)
{
    unsigned char lsbits = (unsigned char)size;

    (void)base;
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
    lsbits |= (unsigned char)(uintptr_t)base;
#endif
    return (lsbits & (align - 1)) == 0;
}

static void swap_words_32(void *a, void *b, size_t n)
{
    do {
        uint32_t t = *(uint32_t *)(a + (n -= 4));
        *(uint32_t *)(a + n) = *(uint32_t *)(b + n);
        *(uint32_t *)(b + n) = t;
    } while (n);
}

static void swap_words_64(void *a, void *b, size_t n)
{
    do {
#ifdef CONFIG_64BIT
        uint64_t t = *(uint64_t *)(a + (n -= 8));
		*(uint64_t *)(a + n) = *(uint64_t *)(b + n);
		*(uint64_t *)(b + n) = t;
#else
        /* Use two 32-bit transfers to avoid base+index+4 addressing */
        uint32_t t = *(uint32_t *)(a + (n -= 4));
        *(uint32_t *)(a + n) = *(uint32_t *)(b + n);
        *(uint32_t *)(b + n) = t;

        t = *(uint32_t *)(a + (n -= 4));
        *(uint32_t *)(a + n) = *(uint32_t *)(b + n);
        *(uint32_t *)(b + n) = t;
#endif
    } while (n);
}

static void swap_bytes(void *a, void *b, size_t n)
{
    do {
        char t = ((char *)a)[--n];
        ((char *)a)[n] = ((char *)b)[n];
        ((char *)b)[n] = t;
    } while (n);
}

#define SWAP_WORDS_64 (swap_func_t)0
#define SWAP_WORDS_32 (swap_func_t)1
#define SWAP_BYTES    (swap_func_t)2

static void do_swap(void *a, void *b, size_t size, swap_func_t swap_func)
{
    if (swap_func == SWAP_WORDS_64)
        swap_words_64(a, b, size);
    else if (swap_func == SWAP_WORDS_32)
        swap_words_32(a, b, size);
    else if (swap_func == SWAP_BYTES)
        swap_bytes(a, b, size);
    else
        swap_func(a, b, (int)size);
}

#define _CMP_WRAPPER ((cmp_r_func_t)0L)

static int do_cmp(const void *a, const void *b, cmp_r_func_t cmp, const void *priv)
{
    if (cmp == _CMP_WRAPPER)
        return (int)((cmp_func_t)(priv))(a, b);
    return cmp(a, b, priv);
}

static int cmpint(const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}
static size_t parent(size_t i, unsigned int lsbit, size_t size)
{
    i -= size;
    i -= size & -(i & lsbit);
    return i / 2;
}

void heap_sort(void *base, size_t num, size_t size,
               cmp_r_func_t cmp_func,
               swap_func_t swap_func,
               const void *priv)
{
    /* pre-scale counters for performance */
    size_t n = num * size, a = (num/2) * size;
    const unsigned int lsbit = size & -size;  /* Used to find parent */

    if (!a)		/* num < 2 || size == 0 */
        return;

    if (!swap_func) {
        if (is_aligned(base, size, 8))
            swap_func = SWAP_WORDS_64;
        else if (is_aligned(base, size, 4))
            swap_func = SWAP_WORDS_32;
        else
            swap_func = SWAP_BYTES;
    }

    for (;;) {
        size_t b, c, d;

        if (a)			/* Building heap: sift down --a */
            a -= size;
        else if (n -= size)	/* Sorting: Extract root to --n */
            do_swap(base, base + n, size, swap_func);
        else			/* Sort complete */
            break;

        for (b = a; c = 2*b + size, (d = c + size) < n;)
            b = do_cmp(base + c, base + d, cmp_func, priv) >= 0 ? c : d;
        if (d == n)	/* Special case last leaf with no sibling */
            b = c;

        /* Now backtrack from "b" to the correct location for "a" */
        while (b != a && do_cmp(base + a, base + b, cmp_func, priv) >= 0)
            b = parent(b, lsbit, size);
        c = b;			/* Where "a" belongs */
        while (b != a) {	/* Shift it into place */
            b = parent(b, lsbit, size);
            do_swap(base + b, base + c, size, swap_func);
        }
    }
}


/* The multithreaded qsort public interface */
void qsort_mt(void *a,
              size_t n,
              size_t es,
              cmp_t *cmp,
              int maxthreads,
              int forkelem)
{
    struct isort *qs;
    struct common c;
    int i, islot;
    bool bailout = true;

    if (n < forkelem)
        goto f1;
    errno = 0;
    /* Try to initialize the resources we need. */
    if (pthread_mutex_init(&c.mtx_al, NULL) != 0)
        goto f1;
    if ((c.pool = calloc(maxthreads, sizeof(struct isort))) == NULL)
        goto f2;
    for (islot = 0; islot < maxthreads; islot++) {
        qs = &c.pool[islot];
        if (pthread_mutex_init(&qs->mtx_st, NULL) != 0)
            goto f3;
        if (pthread_cond_init(&qs->cond_st, NULL) != 0) {
            verify(pthread_mutex_destroy(&qs->mtx_st));
            goto f3;
        }
        qs->st = ts_idle;
        qs->common = &c;
        qs->depth = 2 * log(n);
        if (pthread_create(&qs->id, NULL, isort_thread, qs) != 0) {
            verify(pthread_mutex_destroy(&qs->mtx_st));
            verify(pthread_cond_destroy(&qs->cond_st));
            goto f3;
        }
    }

    /* All systems go. */
    bailout = false;

    /* Initialize common elements. */
    c.swaptype = ((char *) a - (char *) 0) % sizeof(long) || es % sizeof(long)
                 ? 2
                 : es == sizeof(long) ? 0
                                      : 1;
    c.es = es;
    c.cmp = cmp;
    c.forkelem = forkelem;
    c.idlethreads = c.nthreads = maxthreads;

    /* Hand out the first work batch. */
    qs = &c.pool[0];
    verify(pthread_mutex_lock(&qs->mtx_st));
    qs->a = a;
    qs->n = n;
    qs->st = ts_work;
    c.idlethreads--;
    verify(pthread_cond_signal(&qs->cond_st));
    verify(pthread_mutex_unlock(&qs->mtx_st));

    /* Wait for all threads to finish, and free acquired resources. */
    f3:
    for (i = 0; i < islot; i++) {
        qs = &c.pool[i];
        if (bailout) {
            verify(pthread_mutex_lock(&qs->mtx_st));
            qs->st = ts_term;
            verify(pthread_cond_signal(&qs->cond_st));
            verify(pthread_mutex_unlock(&qs->mtx_st));
        }
        verify(pthread_join(qs->id, NULL));
        verify(pthread_mutex_destroy(&qs->mtx_st));
        verify(pthread_cond_destroy(&qs->cond_st));
    }
    free(c.pool);
    f2:
    verify(pthread_mutex_destroy(&c.mtx_al));
    if (bailout) {
        fprintf(stderr, "Resource initialization failed; bailing out.\n");
        f1:
        qsort(a, n, es, cmp);
    }
}

#define thunk       NULL
#define INSERT_SIZE 7
/* Allocate an idle thread from the pool, lock its mutex, change its state to
 * work, decrease the number of idle threads, and return a pointer to its data
 * area.
 * Return NULL, if no thread is available.
 */
static struct isort *allocate_thread(struct common *c)
{
    verify(pthread_mutex_lock(&c->mtx_al));
    for (int i = 0; i < c->nthreads; i++)
        if (c->pool[i].st == ts_idle) {
            verify(pthread_mutex_lock(&c->pool[i].mtx_st));
            c->idlethreads--;
            c->pool[i].st = ts_work;
            verify(pthread_mutex_unlock(&c->mtx_al));
            return (&c->pool[i]);
        }
    verify(pthread_mutex_unlock(&c->mtx_al));
    return (NULL);
}

/* Thread-callable quicksort. */
static void isort_algo(struct isort *qs)
{
    char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
    int d, r, swaptype, swap_cnt, depth;
    void *a;      /* Array of elements. */
    size_t n, es; /* Number of elements; size. */
    cmp_t *cmp;
    int nl, nr;
    struct common *c;
    struct isort *qs2;

    /* Initialize qsort arguments. */
    c = qs->common;
    es = c->es;
    cmp = c->cmp;
    swaptype = c->swaptype;
    a = qs->a;
    n = qs->n;
    depth = qs->depth;

    top:

    swap_cnt = 0;
    if (n < INSERT_SIZE) {
        /* insertion sort */
        for (pm = (char *) a + es; pm < (char *) a + n * es; pm += es)
            for (pl = pm; pl > (char *) a && CMP(thunk, pl - es, pl) > 0;
                 pl -= es)
                swap(pl, pl - es);
        return;
    }
    if(depth == 0)
    {
        /* heap sort */
        heap_sort(a, n, es, _CMP_WRAPPER, NULL, cmpint);
        return;
    }
    /* quick sort */
    pm = (char *) a + (n / 2) * es;
    if (n > INSERT_SIZE) {
        pl = (char *) a;
        pn = (char *) a + (n - 1) * es;
        if (n > 40) {
            d = (n / 8) * es;
            pl = med3(pl, pl + d, pl + 2 * d, cmp, thunk);
            pm = med3(pm - d, pm, pm + d, cmp, thunk);
            pn = med3(pn - 2 * d, pn - d, pn, cmp, thunk);
        }
        pm = med3(pl, pm, pn, cmp, thunk);
    }
    swap(a, pm);
    pa = pb = (char *) a + es;
    pc = pd = (char *) a + (n - 1) * es;

    for (;;) {
        while (pb <= pc && (r = CMP(thunk, pb, a)) <= 0) {
            if (r == 0) {
                swap_cnt = 1;
                swap(pa, pb);
                pa += es;
            }
            pb += es;
        }
        while (pb <= pc && (r = CMP(thunk, pc, a)) >= 0) {
            if (r == 0) {
                swap_cnt = 1;
                swap(pc, pd);
                pd -= es;
            }
            pc -= es;
        }
        if (pb > pc)
            break;
        swap(pb, pc);
        swap_cnt = 1;
        pb += es;
        pc -= es;
    }

    pn = (char *) a + n * es;
    r = min(pa - (char *) a, pb - pa);
    vecswap(a, pb - r, r);
    r = min(pd - pc, pn - pd - es);
    vecswap(pb, pn - r, r);

    if (swap_cnt == 0) { /* Switch to insertion sort */
        r = 1 + n / 4;   /* n >= 7, so r >= 2 */
        for (pm = (char *) a + es; pm < (char *) a + n * es; pm += es)
            for (pl = pm; pl > (char *) a && CMP(thunk, pl - es, pl) > 0;
                 pl -= es) {
                swap(pl, pl - es);
                if (++swap_cnt > r)
                    goto nevermind;
            }
        return;
    }

    nevermind:
    nl = (pb - pa) / es;
    nr = (pd - pc) / es;

    /* Now try to launch subthreads. */
    if (nl > c->forkelem && nr > c->forkelem &&
        (qs2 = allocate_thread(c)) != NULL) {
        qs2->a = a;
        qs2->n = nl;
        qs2->depth = depth - 1;
        verify(pthread_cond_signal(&qs2->cond_st));
        verify(pthread_mutex_unlock(&qs2->mtx_st));
    } else if (nl > 0) {
        qs->a = a;
        qs->n = nl;
        qs->depth = depth - 1;
        isort_algo(qs);
    }
    if (nr > 0) {
        a = pn - nr * es;
        n = nr;
        depth -= 1;
        goto top;
    }
}

/* Thread-callable quicksort. */
static void *isort_thread(void *p)
{
    struct isort *qs, *qs2;
    int i;
    struct common *c;

    qs = p;
    c = qs->common;
    again:
    /* Wait for work to be allocated. */
    verify(pthread_mutex_lock(&qs->mtx_st));
    while (qs->st == ts_idle)
        verify(pthread_cond_wait(&qs->cond_st, &qs->mtx_st));
    verify(pthread_mutex_unlock(&qs->mtx_st));
    if (qs->st == ts_term) {
        return NULL;
    }
    assert(qs->st == ts_work);

    isort_algo(qs);

    verify(pthread_mutex_lock(&c->mtx_al));
    qs->st = ts_idle;
    c->idlethreads++;
    if (c->idlethreads == c->nthreads) {
        for (i = 0; i < c->nthreads; i++) {
            qs2 = &c->pool[i];
            if (qs2 == qs)
                continue;
            verify(pthread_mutex_lock(&qs2->mtx_st));
            qs2->st = ts_term;
            verify(pthread_cond_signal(&qs2->cond_st));
            verify(pthread_mutex_unlock(&qs2->mtx_st));
        }
        verify(pthread_mutex_unlock(&c->mtx_al));
        return NULL;
    }
    verify(pthread_mutex_unlock(&c->mtx_al));
    goto again;
}

#ifndef ELEM_T
#define ELEM_T uint32_t
#endif

int num_compare(const void *a, const void *b)
{
    return (*(ELEM_T *) a - *(ELEM_T *) b);
}

int string_compare(const void *a, const void *b)
{
    return strcmp(*(char **) a, *(char **) b);
}

void *xmalloc(size_t s)
{
    void *p;

    if ((p = malloc(s)) == NULL) {
        perror("malloc");
        exit(1);
    }
    return (p);
}

void usage(void)
{
    fprintf(
            stderr,
            "usage: qsort_mt [-stv] [-f forkelements] [-h threads] [-n elements]\n"
            "\t-l\tRun the libc version of qsort\n"
            "\t-s\tTest with 20-byte strings, instead of integers\n"
            "\t-t\tPrint timing results\n"
            "\t-v\tVerify the integer results\n"
            "Defaults are 1e7 elements, 2 threads, 100 fork elements\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    bool opt_str = false;
    bool opt_time = false;
    bool opt_verify = false;
    bool opt_libc = false;
    int ch, i;
    size_t nelem = 50000000;
    int threads = 2;
    int forkelements = 100;
    ELEM_T *int_elem;
    char *ep;
    char **str_elem;
    struct timeval start, end;
    struct rusage ru;

    gettimeofday(&start, NULL);
    while ((ch = getopt(argc, argv, "f:h:ln:stv")) != -1) {
        switch (ch) {
            case 'f':
                forkelements = (int) strtol(optarg, &ep, 10);
                if (forkelements <= 0 || *ep != '\0') {
                    warnx("illegal number, -f argument -- %s", optarg);
                    usage();
                }
                break;
            case 'h':
                threads = (int) strtol(optarg, &ep, 10);
                if (threads < 0 || *ep != '\0') {
                    warnx("illegal number, -h argument -- %s", optarg);
                    usage();
                }
                break;
            case 'l':
                opt_libc = true;
                break;
            case 'n':
                nelem = (size_t) strtol(optarg, &ep, 10);
                if (nelem == 0 || *ep != '\0') {
                    warnx("illegal number, -n argument -- %s", optarg);
                    usage();
                }
                break;
            case 's':
                opt_str = true;
                break;
            case 't':
                opt_time = true;
                break;
            case 'v':
                opt_verify = true;
                break;
            case '?':
            default:
                usage();
        }
    }

    if (opt_verify && opt_str)
        usage();

    argc -= optind;
    argv += optind;

    if (opt_str) {
        str_elem = xmalloc(nelem * sizeof(char *));
        for (i = 0; i < nelem; i++)
            if (asprintf(&str_elem[i], "%d%d", rand(), rand()) == -1) {
                perror("asprintf");
                exit(1);
            }
    } else {
        int_elem = xmalloc(nelem * sizeof(ELEM_T));
        for (i = 0; i < nelem; i++)
            int_elem[i] = rand() % nelem;
    }
    if (opt_str) {
        if (opt_libc)
            qsort(str_elem, nelem, sizeof(char *), string_compare);
        else
            qsort_mt(str_elem, nelem, sizeof(char *), string_compare, threads,
                     forkelements);
    } else {
        if (opt_libc)
            qsort(int_elem, nelem, sizeof(ELEM_T), num_compare);
        else
            qsort_mt(int_elem, nelem, sizeof(ELEM_T), num_compare, threads,
                     forkelements);
    }
    gettimeofday(&end, NULL);
    getrusage(RUSAGE_SELF, &ru);
    if (opt_verify) {
        for (i = 1; i < nelem; i++)
            if (int_elem[i - 1] > int_elem[i]) {
                fprintf(stderr,
                        "sort error at position %d: "
                        " %d > %d\n",
                        i, int_elem[i - 1], int_elem[i]);
                exit(2);
            }
    }
    if (opt_time)
        printf(
                "%.3g %.3g %.3g\n",
                (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6,
                ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6,
                ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6);
    return (0);
}