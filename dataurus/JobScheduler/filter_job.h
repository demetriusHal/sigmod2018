#pragma once
#include <stdint.h>
#include "Joiner.hpp"
#include "job_scheduler.h"

// Args for allthefilters function
struct allfilters_arg {
    unsigned low;
    unsigned high;
    unsigned prefix;
    vector<uint64_t*> * columns;
    vector<FilterInfo*> * filterPtrs;
    unsigned * new_array;
};


// Args for Self Join
struct self_join_arg {
    unsigned low;
    unsigned high;
    uint64_t * column_values_l;
    uint64_t * column_values_r;
    unsigned * row_ids_matrix;
    unsigned * new_row_ids_matrix;
    unsigned rels_number;
    unsigned new_tbi;
    size_t i;
};

// Args for intermediate functions
struct inter_arg {
    unsigned low;
    unsigned high;
    unsigned prefix;
    uint64_t filter;
    uint64_t * values;
    unsigned * new_array;
    unsigned * old_rids;
    unsigned rel_num;
    unsigned table_index;
};

// Args for Non intermediate functions
struct noninter_arg {
    unsigned low;
    unsigned high;
    unsigned prefix;
    uint64_t filter;
    uint64_t * values;
    unsigned * new_array;
};

class JobSelfJoin: public Job {
public:
    struct self_join_arg & args_;

  JobSelfJoin(struct self_join_arg & args)
  :args_(args)
  {}

  ~JobSelfJoin() {};

  int Run();
};

class JobFilter: public Job {
public:
  virtual int Run() = 0;
  JobFilter() {}
  ~JobFilter() {};
};


// ALL filters class
class JobAllNonInterFindSize : public JobFilter {
public:
    struct allfilters_arg & args_;

    JobAllNonInterFindSize(struct allfilters_arg & args)
    :args_(args)
    {}

    ~JobAllNonInterFindSize() {}

    int Run();
};

class JobAllNonInterFilter : public JobFilter {
public:
    struct allfilters_arg & args_;

    JobAllNonInterFilter(struct allfilters_arg & args)
    :args_(args)
    {}

    ~JobAllNonInterFilter() {}

    int Run();
};


// Less filter classes
class JobLessInterFindSize : public JobFilter {
public:
    struct inter_arg & args_;

    JobLessInterFindSize(struct inter_arg & args)
    :args_(args)
    {}

    ~JobLessInterFindSize() {}

    int Run();
};

class JobLessInterFilter : public JobFilter {
public:
    struct inter_arg & args_;

    JobLessInterFilter(struct inter_arg & args)
    :args_(args)
    {}

    ~JobLessInterFilter() {}

    int Run();
};

// Greater Filter classes
class JobGreaterInterFindSize : public JobFilter {
public:
    struct inter_arg & args_;

    JobGreaterInterFindSize(struct inter_arg & args)
    :args_(args)
    {}

    ~JobGreaterInterFindSize() {}

    int Run();
};

class JobGreaterInterFilter : public JobFilter {
public:
    struct inter_arg & args_;

    JobGreaterInterFilter(struct inter_arg & args)
    :args_(args)
    {}

    ~JobGreaterInterFilter() {}

    int Run();
};

// Equal Filter classes
class JobEqualInterFindSize : public JobFilter {
public:
    struct inter_arg & args_;

    JobEqualInterFindSize(struct inter_arg & args)
    :args_(args)
    {}

    ~JobEqualInterFindSize() {}

    int Run();
};

class JobEqualInterFilter : public JobFilter {
public:
    struct inter_arg & args_;

    JobEqualInterFilter(struct inter_arg & args)
    :args_(args)
    {}

    ~JobEqualInterFilter() {}

    int Run();
};

/*++++++++++++++++++++++++++++++++++++*/
/* NON INTERMERIATE PRALLEL FUNCTIONS */
/*++++++++++++++++++++++++++++++++++++*/

// Less filter classes
class JobLessNonInterFindSize : public JobFilter {
public:
    struct noninter_arg & args_;

    JobLessNonInterFindSize(struct noninter_arg & args)
    :args_(args)
    {}

    ~JobLessNonInterFindSize() {}

    int Run();
};

class JobLessNonInterFilter : public JobFilter {
public:
    struct noninter_arg & args_;

    JobLessNonInterFilter(struct noninter_arg & args)
    :args_(args)
    {}

    ~JobLessNonInterFilter() {}

    int Run();
};

// Greater Filter classes
class JobGreaterNonInterFindSize : public JobFilter {
public:
    struct noninter_arg & args_;

    JobGreaterNonInterFindSize(struct noninter_arg & args)
    :args_(args)
    {}

    ~JobGreaterNonInterFindSize() {}

    int Run();
};

class JobGreaterNonInterFilter : public JobFilter {
public:
    struct noninter_arg & args_;

    JobGreaterNonInterFilter(struct noninter_arg & args)
    :args_(args)
    {}

    ~JobGreaterNonInterFilter() {}

    int Run();
};

// Equal Filter classes
class JobEqualNonInterFindSize : public JobFilter {
public:
    struct noninter_arg & args_;

    JobEqualNonInterFindSize(struct noninter_arg & args)
    :args_(args)
    {}

    ~JobEqualNonInterFindSize() {}

    int Run();
};

class JobEqualNonInterFilter : public JobFilter {
public:
    struct noninter_arg & args_;

    JobEqualNonInterFilter(struct noninter_arg & args)
    :args_(args)
    {}

    ~JobEqualNonInterFilter() {}

    int Run();
};
