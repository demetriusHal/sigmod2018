#ifndef JOB_SCHEDULER_H
#define JOB_SCHEDULER_H

#include "blockingconcurrentqueue.h"

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include "cpu_mapping.h"

#define DISALLOW_COPY_AND_ASSIGN(TypeName)      \
  TypeName(const TypeName&);                    \
  void operator=(const TypeName&)



// The topologies
#ifdef MY_PC
extern int numa[][4];
#endif

#ifdef SIGMOD_1CPU/*<---------------------------ALWAYS DEFIEND IT BEFORE UPLOAD-------------------*/
extern int numa[][20];
#endif

#ifdef SIGMOD_2CPU
extern int numa[][20];
#endif


///    Job ID type
typedef unsigned int JobID;

using moodycamel::BlockingConcurrentQueue;

// Class Job - Abstract
class Job {
 public:
  Job() = default;
  virtual ~Job() {}

  // This method should be implemented by subclasses.
  virtual int Run() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Job);
};

// Class JobScheduler
class JobScheduler {
 public:
  JobScheduler() = default;
  ~JobScheduler() = default;

  bool Init(int num_of_threads, int numa_region) {
    int i;
    num_of_executors_ = num_of_threads;
    id_gen_ = 0;
    executors_counter_ = 0;

    // inti attrs for threads
    pthread_attr_t attr;
    cpu_set_t set;
    pthread_attr_init(&attr);

    // fprintf(stderr, "This system has %d processors configured and "
    //     "%d processors available.\n",
    //     get_nprocs_conf(), get_nprocs());

    executors_ = new JobExecutor*[num_of_executors_];
    for ( i=0; i < num_of_executors_; i++) {
      executors_[i] = new JobExecutor(&jobs_);

      // Bind thread to physical thread
      CPU_ZERO(&set);
      CPU_SET(numa[numa_region][i], &set);
      pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);

      // Create bounded threads
      executors_[i]->Create(&attr);
    }

    return true;
  }

  bool Destroy() {
    if ( executors_ != NULL ) {
      for(int i = 0; i < num_of_executors_; i++) {
        if (executors_[i] != NULL ) {
          delete executors_[i];
        }
      }

      delete []executors_;
      executors_ = NULL;
    }

    return true;
  }

  void Barrier() {
    pthread_barrier_init(&barrier_, NULL, num_of_executors_ + 1);
    for (int i = 0; i < num_of_executors_; ++i) {
      Schedule(new BarrierJob(&barrier_));
    }

    pthread_barrier_wait(&barrier_);
    pthread_barrier_destroy(&barrier_);
  }

  JobID Schedule(Job* job) {
    if (job == nullptr) {
        exit(-1);
    }

    JobID id = ++id_gen_;
    jobs_.enqueue(job);

    return id;
  }

  void Stop(bool force) {
    for(int i = 0; i < num_of_executors_; i++) {
      jobs_.enqueue(nullptr);
    }

    // join/kill executors
    for(int i=0; i<num_of_executors_; i++ ) {
      if ( force ) {
        executors_[i]->Kill();
      } else {
        executors_[i]->Stop();
      }
    }
  }

private:
  // Class BarrierJob
  class BarrierJob : public Job {
   public:

    BarrierJob(pthread_barrier_t *barrier) : barrier_(barrier) {}
    virtual ~BarrierJob() = default;
    int Run() {
      pthread_barrier_wait(barrier_);
      return 0;
    }

   private:
    pthread_barrier_t *barrier_;
  };

  // JobExecutorID
  typedef unsigned int JobExecutorID;

  // Class JobExecutor - handles thread flow
  class JobExecutor {
   public:
    JobExecutor(BlockingConcurrentQueue<Job*> *jobs) : jobs_(jobs), to_exit_(false) {}
    virtual ~JobExecutor() {}

    bool Create(pthread_attr_t * attr) {
      int r = pthread_create(&thread_id_, attr, CallThrFn, this);
      /* Check affinity */
      // int cpus = get_nprocs();
      // pthread_t thread = thread_id_;
      // cpu_set_t set;
      // pthread_getaffinity_np(thread_id_, sizeof(cpu_set_t), &set);
      // for (int j = 0; j < cpus; j++)
      //     if (CPU_ISSET(j, &set))
      //         fprintf(stderr,"[TID] -->  CPU %d\n", j);
      return !r;
    }

    bool Stop() {
      return !pthread_join(thread_id_, NULL);
    }

    bool Kill() {
      to_exit_ = true;
      pthread_kill(thread_id_, SIGKILL);

      return !pthread_join(thread_id_, NULL);
    }

  private:
    // thread function
    static void* CallThrFn(void *pthis) {
      JobExecutor *executor = static_cast<JobExecutor*>(pthis);
      Job *job = nullptr;

      // untill stop/kill
      while(!executor->to_exit_) {
        executor->jobs_->wait_dequeue(job);
        if (job == nullptr) {
          break;
        }

        job->Run();
        delete job;
      }

      pthread_exit((void*) NULL);
    }

    BlockingConcurrentQueue<Job*> *jobs_;

    volatile bool to_exit_;
    pthread_t thread_id_;
  };

  JobID id_gen_;
  int num_of_executors_, executors_counter_;
  pthread_barrier_t barrier_;
  BlockingConcurrentQueue<Job*> jobs_;
  JobExecutor **executors_;

  DISALLOW_COPY_AND_ASSIGN(JobScheduler);
};

#endif    // JOB_SCHEDULER_H