/**
 * @file    parallel_radix_join.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sun Feb 20:19:51 2012
 * @version $Id: parallel_radix_join.c 4548 2013-12-07 16:05:16Z bcagri $
 *
 * @brief  Provides implementations for several variants of Radix Hash Join.
 *
 * (c) 2012, ETH Zurich, Systems Group
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>              /* CPU_ZERO, CPU_SET */
#include <pthread.h>            /* pthread_* */
#include <stdlib.h>             /* malloc, posix_memalign */
#include <sys/time.h>           /* gettimeofday */
#include <stdio.h>              /* printf */
#include <smmintrin.h>          /* simd only for 32-bit keys – SSE4.1 */

#include "parallel_radix_join.h"
#include "prj_params.h"         /* constant parameters */
#include "task_queue.h"         /* task_queue_* */
#include "cpu_mapping.h"        /* get_cpu_id */
#include "rdtsc.h"              /* startTimer, stopTimer */

#include "barrier.h"            /* pthread_barrier_* */
#include "affinity.h"           /* pthread_attr_setaffinity_np */
#include "generator.h"          /* numa_localize() */

#ifdef JOIN_RESULT_MATERIALIZE
#include "tuple_buffer.h"       /* for materialization */
#endif

/** \internal */
#ifndef BARRIER_ARRIVE
/** barrier wait macro */
#define BARRIER_ARRIVE(B,RV)                            \
    RV = pthread_barrier_wait(B);                       \
    if(RV !=0 && RV != PTHREAD_BARRIER_SERIAL_THREAD){  \
        printf("Couldn't wait on barrier\n");           \
        exit(EXIT_FAILURE);                             \
    }
#endif

/** checks malloc() result */
#ifndef MALLOC_CHECK
#define MALLOC_CHECK(M)                                                 \
    if(!M){                                                             \
        printf("[ERROR] MALLOC_CHECK: %s : %d\n", __FILE__, __LINE__);  \
        perror(": malloc() failed!\n");                                 \
        exit(EXIT_FAILURE);                                             \
    }
#endif

/* #define RADIX_HASH(V)  ((V>>7)^(V>>13)^(V>>21)^V) */
#define HASH_BIT_MODULO(K, MASK, NBITS) (((K) & MASK) >> NBITS)

#ifndef NEXT_POW_2
/**
 *  compute the next number, greater than or equal to 32-bit unsigned v.
 *  taken from "bit twiddling hacks":
 *  http://graphics.stanford.edu/~seander/bithacks.html
 */
#define NEXT_POW_2(V)                           \
    do {                                        \
        V--;                                    \
        V |= V >> 1;                            \
        V |= V >> 2;                            \
        V |= V >> 4;                            \
        V |= V >> 8;                            \
        V |= V >> 16;                           \
        V++;                                    \
    } while(0)
#endif

#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

#ifdef SYNCSTATS
#define SYNC_TIMERS_START(A, TID)               \
    do {                                        \
        uint64_t tnow;                          \
        startTimer(&tnow);                      \
        A->localtimer.sync1[0]      = tnow;     \
        A->localtimer.sync1[1]      = tnow;     \
        A->localtimer.sync3         = tnow;     \
        A->localtimer.sync4         = tnow;     \
        A->localtimer.finish_time   = tnow;     \
        if(TID == 0) {                          \
            A->globaltimer->sync1[0]    = tnow; \
            A->globaltimer->sync1[1]    = tnow; \
            A->globaltimer->sync3       = tnow; \
            A->globaltimer->sync4       = tnow; \
            A->globaltimer->finish_time = tnow; \
        }                                       \
    } while(0)

#define SYNC_TIMER_STOP(T) stopTimer(T)
#define SYNC_GLOBAL_STOP(T, TID) if(TID==0){ stopTimer(T); }
#else
#define SYNC_TIMERS_START(A, TID)
#define SYNC_TIMER_STOP(T)
#define SYNC_GLOBAL_STOP(T, TID)
#endif

/** Debug msg logging method */
#ifdef DEBUG
#define DEBUGMSG(COND, MSG, ...)                                    \
    if(COND) { fprintf(stdout, "[DEBUG] "MSG, ## __VA_ARGS__); }
#else
#define DEBUGMSG(COND, MSG, ...)
#endif

/* just to enable compilation with g++ */
#if defined(__cplusplus)
#define restrict __restrict__
#endif

/** An experimental feature to allocate input relations numa-local */
extern int numalocalize;  /* defined in generator.c */

typedef struct arg_t  arg_t;
typedef struct part_t part_t;
typedef struct synctimer_t synctimer_t;
typedef int64_t (*JoinFunction)(const relation_t * const,
                                const relation_t * const,
                                relation_t * const,
                                void * output);

#ifdef SYNCSTATS
/** holds syncronization timing stats if configured with --enable-syncstats */
struct synctimer_t {
    /** Barrier for computation of thread-local histogram */
    uint64_t sync1[3]; /* for rel R and for rel S */
    /** Barrier for end of radix partit. pass-1 */
    uint64_t sync3;
    /** Barrier before join (build-probe) begins */
    uint64_t sync4;
    /** Finish time */
    uint64_t finish_time;
};
#endif

/** holds the arguments passed to each thread */
struct arg_t {
    int32_t ** histR;
    tuple_t *  relR;
    tuple_t *  tmpR;
    int32_t ** histS;
    tuple_t *  relS;
    tuple_t *  tmpS;

    int32_t numR;
    int32_t numS;
    int64_t totalR;
    int64_t totalS;

    task_queue_t **      join_queue;
    task_queue_t **      part_queue;

    pthread_barrier_t * barrier;
    JoinFunction        join_function;
    int64_t result;
    int32_t my_tid;
    int     nthreads;

    /* results of the thread */
    threadresult_t * threadresult;

    /* stats about the thread */
    int32_t        parts_processed;
    uint64_t       timer1, timer2, timer3;
    struct timeval start, end;
#ifdef SYNCSTATS
    /** Thread local timers : */
    synctimer_t localtimer;
    /** Global synchronization timers, only filled in by thread-0 */
    synctimer_t * globaltimer;
#endif
} __attribute__((aligned(CACHE_LINE_SIZE)));

/** holds arguments passed for partitioning */
struct part_t {
    tuple_t *  rel;
    tuple_t *  tmp;
    int32_t ** hist;
    int64_t *  output;
    arg_t   *  thrargs;
    uint64_t   total_tuples;
    uint32_t   num_tuples;
    int32_t    R;
    uint32_t   D;
    int        relidx;  /* 0: R, 1: S */
    uint32_t   padding;
} __attribute__((aligned(CACHE_LINE_SIZE)));

static void *
alloc_aligned(size_t size)
{
    void * ret;
    int rv;
    rv = posix_memalign((void**)&ret, CACHE_LINE_SIZE, size);

    if (rv) {
        perror("alloc_aligned() failed: out of memory");
        return 0;
    }

    return ret;
}

/** \endinternal */

/**
 * @defgroup Radix Radix Join Implementation Variants
 * @{
 */

/**
 *  This algorithm builds the hashtable using the bucket chaining idea and used
 *  in PRO implementation. Join between given two relations is evaluated using
 *  the "bucket chaining" algorithm proposed by Manegold et al. It is used after
 *  the partitioning phase, which is common for all algorithms. Moreover, R and
 *  S typically fit into L2 or at least R and |R|*sizeof(int) fits into L2 cache.
 *
 * @param R input relation R
 * @param S input relation S
 * @param output join results, if JOIN_RESULT_MATERIALIZE defined.
 *
 * @return number of result tuples
 */
int64_t
bucket_chaining_join(const relation_t * const R,
                     const relation_t * const S,
                     relation_t * const tmpR,
                     void * output)
{
    int * next, * bucket;
    const uint32_t numR = R->num_tuples;
    uint32_t N = numR;
    int64_t matches = 0;

    NEXT_POW_2(N);
    /* N <<= 1; */
    const uint32_t MASK = (N-1) << (NUM_RADIX_BITS);

    next   = (int*) malloc(sizeof(int) * numR);
    /* posix_memalign((void**)&next, CACHE_LINE_SIZE, numR * sizeof(int)); */
    bucket = (int*) calloc(N, sizeof(int));

    const tuple_t * const Rtuples = R->tuples;
    for(uint32_t i=0; i < numR; ){
        uint32_t idx = HASH_BIT_MODULO(R->tuples[i].key, MASK, NUM_RADIX_BITS);
        next[i]      = bucket[idx];
        bucket[idx]  = ++i;     /* we start pos's from 1 instead of 0 */

        /* Enable the following tO avoid the code elimination
           when running probe only for the time break-down experiment */
        /* matches += idx; */
    }

    const tuple_t * const Stuples = S->tuples;
    const uint32_t        numS    = S->num_tuples;


    chainedtuplebuffer_t * chainedbuf = (chainedtuplebuffer_t *) output;


    /* Disable the following loop for no-probe for the break-down experiments */
    /* PROBE- LOOP */
    for(uint32_t i=0; i < numS; i++ ){

        uint32_t idx = HASH_BIT_MODULO(Stuples[i].key, MASK, NUM_RADIX_BITS);

        for(int hit = bucket[idx]; hit > 0; hit = next[hit-1]){

            if(Stuples[i].key == Rtuples[hit-1].key){

                /* copy to the result buffer, we skip it */
                tuple_t * joinres = cb_next_writepos(chainedbuf);
                joinres->key      = Rtuples[hit-1].payload; /* R-rid */
                joinres->payload  = Stuples[i].payload;     /* S-rid */
                matches ++;
            }
        }
    }
    /* PROBE-LOOP END  */

    /* clean up temp */
    free(bucket);
    free(next);

    return matches;
}

/**
 * Radix clustering algorithm (originally described by Manegold et al)
 * The algorithm mimics the 2-pass radix clustering algorithm from
 * Kim et al. The difference is that it does not compute
 * prefix-sum, instead the sum (offset in the code) is computed iteratively.
 *
 * @warning This method puts padding between clusters, see
 * radix_cluster_nopadding for the one without padding.
 *
 * @param outRel [out] result of the partitioning
 * @param inRel [in] input relation
 * @param hist [out] number of tuples in each partition
 * @param R cluster bits
 * @param D radix bits per pass
 * @returns tuples per partition.
 */
void
radix_cluster(relation_t * restrict outRel,
              relation_t * restrict inRel,
              int32_t * restrict hist,
              int R,
              int D)
{
    uint32_t i;
    uint32_t M = ((1 << D) - 1) << R;
    uint32_t offset;
    uint32_t fanOut = 1 << D;

    /* the following are fixed size when D is same for all the passes,
       and can be re-used from call to call. Allocating in this function
       just in case D differs from call to call. */
    uint32_t dst[fanOut];

    /* count tuples per cluster */
    for( i=0; i < inRel->num_tuples; i++ ){
        uint32_t idx = HASH_BIT_MODULO(inRel->tuples[i].key, M, R);
        hist[idx]++;
    }
    offset = 0;
    /* determine the start and end of each cluster depending on the counts. */
    for ( i=0; i < fanOut; i++ ) {
        /* dst[i]      = outRel->tuples + offset; */
        /* determine the beginning of each partitioning by adding some
           padding to avoid L1 conflict misses during scatter. */
        dst[i] = offset + i * SMALL_PADDING_TUPLES;
        offset += hist[i];
    }

    /* copy tuples to their corresponding clusters at appropriate offsets */
    for( i=0; i < inRel->num_tuples; i++ ){
        uint32_t idx   = HASH_BIT_MODULO(inRel->tuples[i].key, M, R);
        outRel->tuples[ dst[idx] ] = inRel->tuples[i];
        ++dst[idx];
    }
}

/**
 * This function implements the radix clustering of a given input
 * relations. The relations to be clustered are defined in task_t and after
 * clustering, each partition pair is added to the join_queue to be joined.
 *
 * @param task description of the relation to be partitioned
 * @param join_queue task queue to add join tasks after clustering
 */
void serial_radix_partition(task_t * const task,
                            task_queue_t * join_queue,
                            const int R, const int D)
{
    int i;
    uint32_t offsetR = 0, offsetS = 0;
    const int fanOut = 1 << D;  /*(NUM_RADIX_BITS / NUM_PASSES);*/
    int32_t * outputR, * outputS;

    outputR = (int32_t*)calloc(fanOut+1, sizeof(int32_t));
    outputS = (int32_t*)calloc(fanOut+1, sizeof(int32_t));
    /* TODO: measure the effect of memset() */
    /* memset(outputR, 0, fanOut * sizeof(int32_t)); */
    radix_cluster(&task->tmpR, &task->relR, outputR, R, D);

    /* memset(outputS, 0, fanOut * sizeof(int32_t)); */
    radix_cluster(&task->tmpS, &task->relS, outputS, R, D);

    /* task_t t; */
    for(i = 0; i < fanOut; i++) {
        if(outputR[i] > 0 && outputS[i] > 0) {
            task_t * t = task_queue_get_slot_atomic(join_queue);
            t->relR.num_tuples = outputR[i];
            t->relR.tuples = task->tmpR.tuples + offsetR
                             + i * SMALL_PADDING_TUPLES;
            t->tmpR.tuples = task->relR.tuples + offsetR
                             + i * SMALL_PADDING_TUPLES;
            offsetR += outputR[i];

            t->relS.num_tuples = outputS[i];
            t->relS.tuples = task->tmpS.tuples + offsetS
                             + i * SMALL_PADDING_TUPLES;
            t->tmpS.tuples = task->relS.tuples + offsetS
                             + i * SMALL_PADDING_TUPLES;
            offsetS += outputS[i];

            /* task_queue_copy_atomic(join_queue, &t); */
            task_queue_add_atomic(join_queue, t);
        }
        else {
            offsetR += outputR[i];
            offsetS += outputS[i];
        }
    }
    free(outputR);
    free(outputS);
}

/**
 * This function implements the parallel radix partitioning of a given input
 * relation. Parallel partitioning is done by histogram-based relation
 * re-ordering as described by Kim et al. Parallel partitioning method is
 * commonly used by all parallel radix join algorithms.
 *
 * @param part description of the relation to be partitioned
 */
void
parallel_radix_partition(part_t * const part)
{
    const tuple_t * restrict rel    = part->rel;
    int32_t **               hist   = part->hist;
    int64_t *       restrict output = part->output;

    const uint32_t my_tid     = part->thrargs->my_tid;
    const uint32_t nthreads   = part->thrargs->nthreads;
    const uint32_t num_tuples = part->num_tuples;

    const int32_t  R       = part->R;
    const int32_t  D       = part->D;
    const uint32_t fanOut  = 1 << D;
    const uint32_t MASK    = (fanOut - 1) << R;
    const uint32_t padding = part->padding;

    int64_t sum = 0;
    uint32_t i, j;
    int rv;

    int64_t dst[fanOut+1];

    /* compute local histogram for the assigned region of rel */
    /* compute histogram */
    int32_t * my_hist = hist[my_tid];

    for(i = 0; i < num_tuples; i++) {
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        my_hist[idx] ++;
    }

    /* compute local prefix sum on hist */
    for(i = 0; i < fanOut; i++){
        sum += my_hist[i];
        my_hist[i] = sum;
    }

    //SYNC_TIMER_STOP(&part->thrargs->localtimer.sync1[part->relidx]);
    /* wait at a barrier until each thread complete histograms */
    BARRIER_ARRIVE(part->thrargs->barrier, rv);
    /* barrier global sync point-1 */
    //SYNC_GLOBAL_STOP(&part->thrargs->globaltimer->sync1[part->relidx], my_tid);

    /* determine the start and end of each cluster */
    for(i = 0; i < my_tid; i++) {
        for(j = 0; j < fanOut; j++)
            output[j] += hist[i][j];
    }
    for(i = my_tid; i < nthreads; i++) {
        for(j = 1; j < fanOut; j++)
            output[j] += hist[i][j-1];
    }

    for(i = 0; i < fanOut; i++ ) {
        output[i] += i * padding; //PADDING_TUPLES;
        dst[i] = output[i];
    }
    output[fanOut] = part->total_tuples + fanOut * padding; //PADDING_TUPLES;

    tuple_t * restrict tmp = part->tmp;

    /* Copy tuples to their corresponding clusters */
    for(i = 0; i < num_tuples; i++ ){
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        tmp[dst[idx]] = rel[i];
        ++dst[idx];
    }
}

/**
 * @defgroup SoftwareManagedBuffer Optimized Partitioning Using SW-buffers
 * @{
 */
typedef union {
    struct {
        tuple_t tuples[CACHE_LINE_SIZE/sizeof(tuple_t)];
    } tuples;
    struct {
        tuple_t tuples[CACHE_LINE_SIZE/sizeof(tuple_t) - 1];
        int64_t slot;
    } data;
} cacheline_t;

#define TUPLESPERCACHELINE (CACHE_LINE_SIZE/sizeof(tuple_t))

/** @} */
/**
 * The main thread of parallel radix join. It does partitioning in parallel with
 * other threads and during the join phase, picks up join tasks from the task
 * queue and calls appropriate JoinFunction to compute the join task.
 *
 * @param param
 *
 * @return
 */
void *
prj_thread(void * param)
{
    arg_t * args   = (arg_t*) param;
    int32_t my_tid = args->my_tid;

    const int fanOut = 1 << (NUM_RADIX_BITS / NUM_PASSES);//PASS1RADIXBITS;
    const int R = (NUM_RADIX_BITS / NUM_PASSES);//PASS1RADIXBITS;
    const int D = (NUM_RADIX_BITS - (NUM_RADIX_BITS / NUM_PASSES));//PASS2RADIXBITS;

    uint64_t results = 0;
    int i;
    int rv;

    part_t part;
    task_t * task;
    task_queue_t * part_queue;
    task_queue_t * join_queue;

    int64_t * outputR = (int64_t *) calloc((fanOut+1), sizeof(int64_t));
    int64_t * outputS = (int64_t *) calloc((fanOut+1), sizeof(int64_t));
    //MALLOC_CHECK((outputR && outputS));

    int numaid = get_numa_id(my_tid);
    //int numaid = get_numa_region_id(my_tid);
    part_queue = args->part_queue[numaid];
    join_queue = args->join_queue[numaid];

    args->histR[my_tid] = (int32_t *) calloc(fanOut, sizeof(int32_t));
    args->histS[my_tid] = (int32_t *) calloc(fanOut, sizeof(int32_t));

    /* in the first pass, partitioning is done together by all threads */

    args->parts_processed = 0;

    /* wait at a barrier until each thread starts and then start the timer */
    BARRIER_ARRIVE(args->barrier, rv);

    /* if monitoring synchronization stats */
   //SYNC_TIMERS_START(args, my_tid);

    /********** 1st pass of multi-pass partitioning ************/
    part.R       = 0;
    part.D       = NUM_RADIX_BITS / NUM_PASSES; //PASS1RADIXBITS
    part.thrargs = args;
    part.padding = PADDING_TUPLES;

    /* 1. partitioning for relation R */
    part.rel          = args->relR;
    part.tmp          = args->tmpR;
    part.hist         = args->histR;
    part.output       = outputR;
    part.num_tuples   = args->numR;
    part.total_tuples = args->totalR;
    part.relidx       = 0;

    parallel_radix_partition(&part);

    /* 2. partitioning for relation S */
    part.rel          = args->relS;
    part.tmp          = args->tmpS;
    part.hist         = args->histS;
    part.output       = outputS;
    part.num_tuples   = args->numS;
    part.total_tuples = args->totalS;
    part.relidx       = 1;

    parallel_radix_partition(&part);

    /* wait at a barrier until each thread copies out */
    BARRIER_ARRIVE(args->barrier, rv);

    /********** end of 1st partitioning phase ******************/
    /* 3. first thread creates partitioning tasks for 2nd pass */
    if(my_tid == 0) {
        /* For Debugging: */
        /* int numnuma = get_num_numa_regions(); */
        /* int correct_numa_mapping = 0, wrong_numa_mapping = 0; */
        /* int counts[4] = {0, 0, 0, 0}; */
        for(i = 0; i < fanOut; i++) {
            int32_t ntupR = outputR[i+1] - outputR[i] - PADDING_TUPLES;
            int32_t ntupS = outputS[i+1] - outputS[i] - PADDING_TUPLES;

            if(ntupR > 0 && ntupS > 0) {
                /* Determine the NUMA node of each partition: */
                void * ptr = (void*)&((args->tmpR + outputR[i])[0]);
                int pq_idx = get_numa_node_of_address(ptr);

                /* For Debugging: */
                /* void * ptr2 = (void*)&((args->tmpS + outputS[i])[0]); */
                /* int pq_idx2 = get_numa_node_of_address(ptr2); */
                /* if(pq_idx != pq_idx2) */
                /*     wrong_numa_mapping ++; */
                /* else */
                /*     correct_numa_mapping ++; */
                /* int numanode_of_mem = get_numa_node_of_address(ptr); */
                /* int pq_idx = i / (fanOut / numnuma); */
                /* if(numanode_of_mem == pq_idx) */
                /*     correct_numa_mapping ++; */
                /* else */
                /*     wrong_numa_mapping ++; */
                /* pq_idx = numanode_of_mem; */
                /* counts[numanode_of_mem] ++; */
                /* counts[pq_idx] ++; */

                task_queue_t * numalocal_part_queue = args->part_queue[pq_idx];

                task_t * t = task_queue_get_slot(numalocal_part_queue);

                t->relR.num_tuples = t->tmpR.num_tuples = ntupR;
                t->relR.tuples = args->tmpR + outputR[i];
                t->tmpR.tuples = args->relR + outputR[i];

                t->relS.num_tuples = t->tmpS.num_tuples = ntupS;
                t->relS.tuples = args->tmpS + outputS[i];
                t->tmpS.tuples = args->relS + outputS[i];


                task_queue_add(numalocal_part_queue, t);

            }
        }

        /* debug partitioning task queue */
        DEBUGMSG(1, "Pass-2: # partitioning tasks = %d\n", part_queue->count);

        /* DEBUG NUMA MAPPINGS */
        /* printf("Correct NUMA-mappings = %d, Wrong = %d\n", */
        /*        correct_numa_mapping, wrong_numa_mapping); */
        /* printf("Counts -- 0=%d, 1=%d, 2=%d, 3=%d\n",  */
        /*        counts[0], counts[1], counts[2], counts[3]); */
    }

    //SYNC_TIMER_STOP(&args->localtimer.sync3);
    /* wait at a barrier until first thread adds all partitioning tasks */
    BARRIER_ARRIVE(args->barrier, rv);
    /* global barrier sync point-3 */
    //SYNC_GLOBAL_STOP(&args->globaltimer->sync3, my_tid);

    /************ 2nd pass of multi-pass partitioning ********************/
    /* 4. now each thread further partitions and add to join task queue **/

#if NUM_PASSES==1
    /* If the partitioning is single pass we directly add tasks from pass-1 */
    //fprintf(stderr, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    task_queue_t * swap = join_queue;
    join_queue = part_queue;
    /* part_queue is used as a temporary queue for handling skewed parts */
    part_queue = swap;

#elif NUM_PASSES==2

    while((task = task_queue_get_atomic(part_queue))){

        serial_radix_partition(task, join_queue, R, D);

    }

#else
#warning Only 2-pass partitioning is implemented, set NUM_PASSES to 2!
#endif

    free(outputR);
    free(outputS);

    //SYNC_TIMER_STOP(&args->localtimer.sync4);
    /* wait at a barrier until all threads add all join tasks */
    BARRIER_ARRIVE(args->barrier, rv);
    /* global barrier sync point-4 */
    //SYNC_GLOBAL_STOP(&args->globaltimer->sync4, my_tid);

    DEBUGMSG((my_tid == 0), "Number of join tasks = %d\n", join_queue->count);

#ifdef JOIN_RESULT_MATERIALIZE
    chainedtuplebuffer_t * chainedbuf = chainedtuplebuffer_init();
#else
    void * chainedbuf = NULL;
#endif

    while((task = task_queue_get_atomic(join_queue))){
        /* do the actual join. join method differs for different algorithms,
           i.e. bucket chaining, histogram-based, histogram-based with simd &
           prefetching  */
        results += args->join_function(&task->relR, &task->relS, &task->tmpR, chainedbuf);

        args->parts_processed ++;
    }

    args->result = results;

#ifdef JOIN_RESULT_MATERIALIZE
    args->threadresult->nresults = results;
    args->threadresult->threadid = my_tid;
    args->threadresult->results  = (void *) chainedbuf;
#endif

    /* this thread is finished */
    //SYNC_TIMER_STOP(&args->localtimer.finish_time);

    /* global finish time */
    //SYNC_GLOBAL_STOP(&args->globaltimer->finish_time, my_tid);

    return 0;
}

/**
 * The template function for different joins: Basically each parallel radix join
 * has a initialization step, partitioning step and build-probe steps. All our
 * parallel radix implementations have exactly the same initialization and
 * partitioning steps. Difference is only in the build-probe step. Here are all
 * the parallel radix join implemetations and their Join (build-probe) functions:
 *
 * - PRO,  Parallel Radix Join Optimized --> bucket_chaining_join()
 */
result_t *
join_init_run(relation_t * relR, relation_t * relS, JoinFunction jf, int nthreads)
{
    int i, rv;
    pthread_t tid[nthreads];
    pthread_attr_t attr;
    pthread_barrier_t barrier;
    cpu_set_t set;
    arg_t args[nthreads];

    int32_t ** histR, ** histS;
    tuple_t * tmpRelR, * tmpRelS;
    int32_t numperthr[2];
    int64_t result = 0;

    /* task_queue_t * part_queue, * join_queue; */
    int numnuma = get_num_numa_regions();
    task_queue_t * part_queue[numnuma];
    task_queue_t * join_queue[numnuma];

    for(i = 0; i < numnuma; i++){
        part_queue[i] = task_queue_init(FANOUT_PASS1);
        join_queue[i] = task_queue_init((1<<NUM_RADIX_BITS));
    }

    result_t * joinresult = 0;
    joinresult = (result_t *) malloc(sizeof(result_t));

#ifdef JOIN_RESULT_MATERIALIZE
    joinresult->resultlist = (threadresult_t *) malloc(sizeof(threadresult_t)
                                                       * nthreads);
#endif

    /* allocate temporary space for partitioning */
    tmpRelR = (tuple_t*) alloc_aligned(relR->num_tuples * sizeof(tuple_t) +
                                       RELATION_PADDING);
    tmpRelS = (tuple_t*) alloc_aligned(relS->num_tuples * sizeof(tuple_t) +
                                       RELATION_PADDING);
    //MALLOC_CHECK((tmpRelR && tmpRelS));
    /** Not an elegant way of passing whether we will numa-localize, but this
        feature is experimental anyway. */
    /*if(numalocalize) {
        uint64_t numwithpad;

        numwithpad = (relR->num_tuples * sizeof(tuple_t) +
                      RELATION_PADDING)/sizeof(tuple_t);
        numa_localize(tmpRelR, numwithpad, nthreads);

        numwithpad = (relS->num_tuples * sizeof(tuple_t) +
                      RELATION_PADDING)/sizeof(tuple_t);
        numa_localize(tmpRelS, numwithpad, nthreads);
    }*/


    /* allocate histograms arrays, actual allocation is local to threads */
    histR = (int32_t**) alloc_aligned(nthreads * sizeof(int32_t*));
    histS = (int32_t**) alloc_aligned(nthreads * sizeof(int32_t*));
    //MALLOC_CHECK((histR && histS));

    rv = pthread_barrier_init(&barrier, NULL, nthreads);
    if(rv != 0){
        printf("[ERROR] Couldn't create the barrier\n");
        exit(EXIT_FAILURE);
    }

    pthread_attr_init(&attr);

#ifdef SYNCSTATS
    /* thread-0 keeps track of synchronization stats */
    args[0].globaltimer = (synctimer_t*) malloc(sizeof(synctimer_t));
#endif

    /* first assign chunks of relR & relS for each thread */
    numperthr[0] = relR->num_tuples / nthreads;
    numperthr[1] = relS->num_tuples / nthreads;
    for(i = 0; i < nthreads; i++){
        int cpu_idx = get_cpu_id(i);

        DEBUGMSG(1, "Assigning thread-%d to CPU-%d\n", i, cpu_idx);
        //fprintf(stderr, "Assigning thread-%d to CPU-%d\n", i, cpu_idx);
        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);  //TODO CHANGE

        args[i].relR = relR->tuples + i * numperthr[0];
        args[i].tmpR = tmpRelR;
        args[i].histR = histR;

        args[i].relS = relS->tuples + i * numperthr[1];
        args[i].tmpS = tmpRelS;
        args[i].histS = histS;

        args[i].numR = (i == (nthreads-1)) ?
            (relR->num_tuples - i * numperthr[0]) : numperthr[0];
        args[i].numS = (i == (nthreads-1)) ?
            (relS->num_tuples - i * numperthr[1]) : numperthr[1];
        args[i].totalR = relR->num_tuples;
        args[i].totalS = relS->num_tuples;

        args[i].my_tid = i;
        args[i].part_queue = part_queue;
        args[i].join_queue = join_queue;

        args[i].barrier       = &barrier;
        args[i].join_function = jf;
        args[i].nthreads      = nthreads;
        args[i].threadresult  = &(joinresult->resultlist[i]);

        rv = pthread_create(&tid[i], &attr, prj_thread, (void*)&args[i]);
        if (rv){
            fprintf(stderr,"[ERROR] return code from pthread_create() is %d\n", rv);
            exit(-1);
        }
    }

    /* wait for threads to finish */
    for(i = 0; i < nthreads; i++){
        pthread_join(tid[i], NULL);
        result += args[i].result;
    }
    joinresult->totalresults = result;
    joinresult->nthreads     = nthreads;

#ifdef SYNCSTATS
/* #define ABSDIFF(X,Y) (((X) > (Y)) ? ((X)-(Y)) : ((Y)-(X))) */
    fprintf(stdout, "TID JTASKS T1.1 T1.1-IDLE T1.2 T1.2-IDLE "\
            "T3 T3-IDLE T4 T4-IDLE T5 T5-IDLE\n");
    for(i = 0; i < nthreads; i++){
        synctimer_t * glob = args[0].globaltimer;
        synctimer_t * local = & args[i].localtimer;
        fprintf(stdout,
                "%d %d %llu %llu %llu %llu %llu %llu %llu %llu "\
                "%llu %llu\n",
                (i+1), args[i].parts_processed, local->sync1[0],
                glob->sync1[0] - local->sync1[0],
                local->sync1[1] - glob->sync1[0],
                glob->sync1[1] - local->sync1[1],
                local->sync3 - glob->sync1[1],
                glob->sync3 - local->sync3,
                local->sync4 - glob->sync3,
                glob->sync4 - local->sync4,
                local->finish_time - glob->sync4,
                glob->finish_time - local->finish_time);
    }
#endif

    /* clean up */
    for(i = 0; i < nthreads; i++) {
        free(histR[i]);
        free(histS[i]);
    }
    free(histR);
    free(histS);

    for(i = 0; i < numnuma; i++){
        task_queue_free(part_queue[i]);
        task_queue_free(join_queue[i]);
    }

    free(tmpRelR);
    free(tmpRelS);
#ifdef SYNCSTATS
    free(args[0].globaltimer);
#endif

    return joinresult;
}

/** \copydoc PRO */
result_t *
PRO(relation_t * relR, relation_t * relS, int nthreads)
{
    return join_init_run(relR, relS, bucket_chaining_join, nthreads);
}
/** @} */