Test:  [2023 年暑期 Linux 核心課程第 1 次作業](https://hackmd.io/@sysprog/linux2023-summer-quiz0)
source code: [lkta](https://github.com/zoanana990/lkta)

## 測驗 $\alpha$, s-tree
```c
AAAA = st_replace_right(del, least)
BBBB = st_update(root, st_parent(least))
CCCC = st_replace_left(del, most)
DDDD = st_update(root, st_parent(most))
EEEE = st_update(root, parent)
FFFF = st_left(n)
GGGG = st_right(n)
```
BBBB 和 DDDD 錯誤， debug 中

### 延伸問題:
1. 解釋上述程式碼運作原理
2. 指出上述程式碼可改進之處，特別跟 AVL tree 和 red-black tree 相比，並予以實作
3. 設計效能評比程式，探討上述程式碼和 red-black tree 效能落差

## 測驗 $\beta$, Alignment
```c
static inline uintptr_t align_up(uintptr_t sz, size_t alignment)
{
	uintptr_t mask = alignment - 1;
	if ((alignment & mask) == 0) {  /* power of two */
		return (sz + mask) & ~mask; /* MMMM */    
	}
	return (((sz + mask) / alignment) * alignment);
}
```
Answer:
```c
MMMM = (sz + mask) & ~mask
```

### 程式碼的運作方式
這題其實我想不到 if 迴圈以外的情況，因為 `size_t` 在 kernel 中其實是個 `unsigned integer` 所以應該不會有出迴圈的情況，接下來就是思考邏輯了
1. `120 -> 120`
2. `121 -> 124`

首先後面兩位一定要是 0 因此需要一個 `& ~mask` 來清掉後面兩位，接下來就是 `padding` ，只要尾數是 1 需要填充 3，2 需要填充 2，但是爆掉的話會有 `& ~mask` 幫你清乾淨，因此可以大方地把 `mask` 加上去就完工了

如果試了一個早上試不出來的話（就是我）可以看一下延伸閱讀中的 `algin_up` 

### 找到 Linux Kernel 中使用到的 align_up
Linux kernel 中的 `align_up` 是一個巨集 `ALIGN`：
```c
/* include/linux/aign.h */
#define ALIGN(x, a)		__ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#define __ALIGN_MASK(x, mask)	__ALIGN_KERNEL_MASK((x), (mask))

/* tools/include/linux/mm.h */
#define __ALIGN_KERNEL(x, a)		__ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask)	(((x) + (mask)) & ~(mask))
```
其實程式碼的機制和上面的一模一樣，這邊來看看應用面，從 linker script 做的 memory alignment 到乙太網路驅動、媒體的驅動裝置、 wifi 等等，都會使用到這個巨集

## 測驗 $\gamma$, qsort
Answer:
```c
HHHH = pthread_cond_wait(&qs->cond_st, &qs->mtx_st)
JJJJ = pthread_cond_signal(&qs2->cond_st)
```

### 程式碼運作原理
首先是將我們想要測試的引數丟到命令列當中，這邊可以看一下程式碼的實作
```c=434
while ((ch = getopt(argc, argv, "f:h:ln:stv")) != -1)
    ...
```
其中 `getopt` 就是從命令列得到 `optstring` 中的引述選項，其中引述後面如果沒有冒號，代表僅需一個字元選項；有一個冒號代表後面需要一個參數，可以用空白隔開；如果後面有兩個冒號代表後面的參數可以寫也可以不寫
```c
t      -> -t
t:     -> -t 10
t::    -> -t 10 or -t
```
得到字元之後，可以將引數的字串轉為整數、改變選項等等，下面舉兩個例子
```c=436
        case 'f':
            forkelements = (int) strtol(optarg, &ep, 10);
```
這邊是使用 `strtol` string to long integer 的函數將字串轉為整數。

```c=463
        case 't':
            opt_time = true;
```
這邊則是將測量出來的時間數據印出來

接下來看一下 `qsort_mt` 的實作方式，這邊可以提供 `libc` 版本的 `qsort` 及題目所給的 `qsort_mt` 的排序方式，先看一下 `qsort_mt` 的引數

```c=503
            qsort_mt(int_elem, nelem, sizeof(ELEM_T), num_compare, threads,
                     forkelements);
```
即宣告所需的參數做對照
```c=113
/* The multi-threaded qsort public interface */
void qsort_mt(void *a,
              size_t n,
              size_t es,
              cmp_t *cmp,
              int maxthreads,
              int forkelem)
```
再對照一下 `qsort` in `libc`
```c
void qsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *));
```
我們可以看到 `base` 和 `a` 都是指陣列的起始位置，`nel` 和 `n` 是指陣列的元素個數，`width` 和 `es` 是指每個元素的大小，`compar` 和 `cmp` 是指比較函式的函式指標。

開始分析 `qsort_mt`，首先初始化 `pthread` 的 `mutex` 進行初始化
```c=130
    if (pthread_mutex_init(&c.mtx_al, NULL) != 0)
        goto f1;
```
接下來分配執行緒的空間，並將各執行緒的 `struct qsort` 初始化並創建執行緒，細節如註解所示:
```c=134
    /* 分配執行緒空間 */
    if ((c.pool = calloc(maxthreads, sizeof(struct qsort))) == NULL)
        goto f2;
    /* 將每一個執行緒指定給一個 qsort structure */
    for (islot = 0; islot < maxthreads; islot++) {
        qs = &c.pool[islot];
        /* 初始化互斥鎖 */
        if (pthread_mutex_init(&qs->mtx_st, NULL) != 0)
            goto f3;
        /* 動態初始化條件變數 */
        if (pthread_cond_init(&qs->cond_st, NULL) != 0) {
            verify(pthread_mutex_destroy(&qs->mtx_st));
            goto f3;
        }
        /* 將狀態設置為 idle */
        qs->st = ts_idle;
        /* 共享 common structure 的資料 */
        qs->common = &c;
        /* 創造一個執行緒執行 qsort_thread */
        if (pthread_create(&qs->id, NULL, qsort_thread, qs) != 0) {
            verify(pthread_mutex_destroy(&qs->mtx_st));
            verify(pthread_cond_destroy(&qs->cond_st));
            goto f3;
        }
    }
```
接下來，父執行緒繼續執行接下來的程式碼，而子執行緒則是執行 `qsort_thread`


### 以 Thread Sanitizer 找出上述程式碼的 data race 並著手修正


### 研讀專題: `lib/sort.c`，提出上述程式碼效能改進之規劃並予以實作