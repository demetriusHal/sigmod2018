#pragma once
#include <unordered_map>
#include <sys/time.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdint>
#include <string>
#include <map>

#include "Relation.hpp"
#include "Parser.hpp"
#include "table_t.hpp"
#include "parallel_radix_join.h"
#include "create_job.h"
#include "checksum_job.h"

/* THread pool Includes */
/*----------------*/

//#define time
//#define prints
#define MASTER_THREADS  2
#define THREAD_NUM_1CPU 10
#define THREAD_NUM_2CPU 10
#define NUMA_REG1 0
#define NUMA_REG2 1

using namespace std;


class JTree;
struct ColumnInfo;
// Use it in filter cpp for the result of the map
typedef std::map<Selection, cached_t*>::iterator cache_res;
typedef std::map<SelectInfo, ColumnInfo> columnInfoMap;

//caching info
extern std::map<Selection, cached_t*> idxcache;
extern pthread_mutex_t cache_mtx;
/*
 * Prints a column
 * Arguments : A @column of column_t type
 */
void PrintColumn(column_t *column);


class Joiner {
    std::vector<Relation> relations; // The relations that might be joined

    public:

    /* 2 Jobs scheduler */
    int mst;
    JobScheduler job_scheduler;
    //JobScheduler job_scheduler2;


    /* Initialize the row_id Array */
    void RowIdArrayInit(QueryInfo &query_info);

    // Add relation
    void addRelation(const char* fileName);

    // Get relation
    Relation& getRelation(unsigned id);

    // Get the total number of relations
    int getRelationsCount();

    table_t*    CreateTableTFromId(unsigned rel_id, unsigned rel_binding);
    relation_t* CreateRowRelationT(uint64_t * column, unsigned size);
    relation_t* CreateRelationT(table_t * table, SelectInfo &sel_info);
    table_t*    CreateTableT(result_t * result, table_t * table_r, table_t * table_s, columnInfoMap & cmap);
    void        CheckSumOnTheFly(result_t * result, table_t * table_r, table_t * table_s, columnInfoMap & cmap, std::vector<SelectInfo> selections, string &);
    void        AddColumnToTableT(SelectInfo &sel_info, table_t *table);

    // The select functions
    void SelectAll(std::vector<FilterInfo*> & filterPtrs, table_t* table);
    void Select(FilterInfo &sel_info, table_t *table, ColumnInfo* columnInfo);
    void SelectEqual(table_t *table, int filter);
    void SelectGreater(table_t *table, int filter);
    void SelectLess(table_t *table, int filter);
    cached_t* Cached_SelectEqual(uint64_t fil, cache_res& cache_info, table_t* table);

    // Joins a given set of relations
    template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
    T1* join(T1 *table_r, T1 *table_s, T2 &pred_info, T3 & cmap, T4 isRoot, std::vector<T5> & selections, T6 leafs, T7 & result_str) {

        relation_t * r1;
        relation_t * r2;

        //HERE WE CHECK FOR CACHED PARTITIONS
        Cacheinf c;
        size_t threads = THREAD_NUM_1CPU; // + THREAD_NUM_2CPU;




        Selection left(pred_info.left);
        Selection right(pred_info.right);
        int ch_flag;

        // if (table_r->ch_filter == NULL) {
            if (leafs&1) {
                pthread_mutex_lock(&cache_mtx);
                ch_flag = (idxcache.find(left) != idxcache.end());
                pthread_mutex_unlock(&cache_mtx);
                if (ch_flag == 1) {
                    r1 = (relation_t *)malloc(sizeof(relation_t));
                    r1->num_tuples = table_r->tups_num;
                    pthread_mutex_lock(&cache_mtx);
                    c.R = idxcache[left];
                    pthread_mutex_unlock(&cache_mtx);
                }
                else {

                    r1 = CreateRelationT(table_r, pred_info.left);
                    c.R = (cached_t *) calloc(threads, sizeof(cached_t));
                }
            } else {

                r1 = CreateRelationT(table_r, pred_info.left);
                c.R = NULL;
            }
        // } else {
        //     // We have cached filter
        //     std::cerr << "In left cached filter" << '\n';
        //     r1 = (relation_t *)malloc(sizeof(relation_t));
        //     r1->num_tuples = table_r->tups_num;
        //     c.R = table_r->ch_filter;
        // }


        // Check for cached filter
        //if (table_s->ch_filter == NULL) {
            if (leafs&2) {
                pthread_mutex_lock(&cache_mtx);
                ch_flag = (idxcache.find(right) != idxcache.end());
                pthread_mutex_unlock(&cache_mtx);
                if (ch_flag == 1) {
                    r2 = (relation_t *)malloc(sizeof(relation_t));
                    r2->num_tuples = table_s->tups_num;
                    pthread_mutex_lock(&cache_mtx);
                    c.S = idxcache[right];
                    pthread_mutex_unlock(&cache_mtx);
                }
                else {
                    r2 = CreateRelationT(table_s, pred_info.right);
                    c.S = (cached_t *) calloc(threads, sizeof(cached_t));;
                }
            } else {
                r2 = CreateRelationT(table_s, pred_info.right);
                c.S = NULL;
            }
        // } else {
        //     // We have cached filter
        //     std::cerr << "In right cached filter" << '\n';
        //     r2 = (relation_t *)malloc(sizeof(relation_t));
        //     r2->num_tuples = table_s->tups_num;
        //     c.S = table_s->ch_filter;
        // }

        result_t * res  = PRO(r1, r2, threads, c, job_scheduler);

        //if (table_r->ch_filter == NULL) {
            if (leafs&1) {
                free(r1);
                pthread_mutex_lock(&cache_mtx);
                idxcache[left] = c.R;
                pthread_mutex_unlock(&cache_mtx);
            }
        //}

        //if (table_s->ch_filter == NULL) {
            if (leafs&2) {
                free(r2);
                pthread_mutex_lock(&cache_mtx);
                idxcache[right] = c.S;
                pthread_mutex_unlock(&cache_mtx);
            }
        //}


        #ifdef time
        gettimeofday(&end, NULL);
        timeRadixJoin += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        #endif

        table_t *temp = NULL;
        // On root dont create a resilting table just get the checksums
        if (isRoot) {
            CheckSumOnTheFly(res, table_r, table_s, cmap, selections, result_str);
        } else {
            temp = CreateTableT(res, table_r, table_s, cmap);
        }


        /* Free the tables and the result of radix */
        if (table_r->ch_filter != NULL) free(table_r->ch_filter);
        if (table_s->ch_filter != NULL) free(table_s->ch_filter);
        table_r->ch_filter = NULL;
        table_s->ch_filter = NULL;

        free(table_r->row_ids);
        delete table_r->column_j;
        delete table_r;
        free(table_s->row_ids);
        delete table_s->column_j;
        delete table_s;
        free(res->resultlist);
        free(res);

        return temp;
    }

    table_t* SelfJoin(table_t *table, PredicateInfo *pred_info, columnInfoMap & cmap);
    table_t* SelfJoinCheckSumOnTheFly(table_t *table, PredicateInfo *predicate_ptr, columnInfoMap & cmap, std::vector<SelectInfo> selections, string & result_str);

    //caching info
    //std::map<Selection, cached_t*> idxcache;
};

#include "QueryPlan.hpp"

int cleanQuery(QueryInfo &);
