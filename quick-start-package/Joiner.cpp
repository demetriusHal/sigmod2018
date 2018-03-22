#include "Joiner.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <array>
#include <utility>
#include <vector>
#include <map>
#include <set>
#include <pthread.h>



#include "Parser.hpp"
#include "QueryPlan.hpp"
#include "header.hpp"
#include "Joiner.hpp"

using namespace std;

#define time

// #define TIME_DETAILS
// #include <sstream>
// string timeDetStr = "";

bool done_testing = false;

/* Timing variables */
double timeSelfJoin = 0;
double timeConstruct = 0;
double timeSelectFilter = 0;
double timeLowJoin = 0;
double timeCreateTable = 0;
double timeAddColumn = 0;
double timeTreegen = 0;
double timeCheckSum = 0;
double timeBuildPhase = 0;
double timeProbePhase = 0;
double timeRadixJoin = 0;
double timeCreateRelationT = 0;
double timeCreateTableT = 0;

 int cleanQuery(QueryInfo &info) {
     /* Remove weak filters */
     int changed = 0;

     map<SelectInfo, FilterInfo> filter_mapG;
     map<SelectInfo, FilterInfo> filter_mapL;
     set<FilterInfo> filters;

     for (auto filter: info.filters) {
         if (filter.comparison == '<') {
             if ((filter_mapL.find(filter.filterColumn) == filter_mapL.end())
                     || (filter_mapL[filter.filterColumn].constant > filter.constant)) {
                 filter_mapL[filter.filterColumn] = filter;
             }

         }
         else if (filter.comparison == '>'){
             if ((filter_mapG.find(filter.filterColumn) == filter_mapG.end())
                     || (filter_mapG[filter.filterColumn].constant < filter.constant)) {
                 filter_mapG[filter.filterColumn] = filter;
             }
         }
         else {
             filters.insert(filter);
         }
     }

     info.filters.clear();
     vector<FilterInfo> newfilters;
     for (auto filter: filters) {
         info.filters.push_back(filter);
     }

     for (std::map<SelectInfo,FilterInfo>::iterator it=filter_mapG.begin(); it!=filter_mapG.end(); ++it) {
         info.filters.push_back(it->second);
     }

     for (std::map<SelectInfo,FilterInfo>::iterator it=filter_mapL.begin(); it!=filter_mapL.end(); ++it) {
         info.filters.push_back(it->second);
     }

     /* Remove duplicate predicates */
     changed = 0;
     set <PredicateInfo> pred_set;
     for (auto pred: info.predicates) {
        if (!(pred.left < pred.right)) {
             SelectInfo tmp = pred.left;
             pred.left = pred.right;
             pred.right = tmp;
         //    cerr << "swapped" << endl;
         }

         if (pred_set.find(pred) != pred_set.end()) {
             changed = 1;
             continue;
         }
         pred_set.insert(pred);
     }

     if (changed == 0) {
         return 0;
     }

     info.predicates.clear();
     for (auto pred: pred_set) {
         info.predicates.push_back(pred);
     }

     return 0;
 }


/* ================================ */
/* Table_t <=> Relation_t fuctnions */
/* ================================ */
relation_t * Joiner::CreateRelationT(table_t * table, SelectInfo &sel_info) {

#ifdef time
    struct timeval start;
    gettimeofday(&start, NULL);
#endif

    /* Create a new column_t for table */
    std::vector<unsigned> &relation_mapping = table->relations_bindings;
    matrix & row_ids = *table->relations_row_ids;

    /* Get the relation from joiner */
    Relation &rel = getRelation(sel_info.relId);
    uint64_t * values = rel.columns.at(sel_info.colId);

    /* Create the relatin_t */
    relation_t * new_relation = (relation_t *) malloc(sizeof(relation_t));

    if (table->intermediate_res) {
        unsigned table_index = -1;
        unsigned relation_binding = sel_info.binding;

        /* Get the right index from the relation id table */
        for (size_t index = 0; index < relation_mapping.size(); index++) {
            if (relation_mapping[index] == relation_binding){
                table_index = index;
            }
        }

        /* Error msg for debuging */
        if (table_index == -1)
            std::cerr << "At AddColumnToTableT, Id not matchin with intermediate result vectors" << '\n';

        /* Initialize relation */
        uint32_t size = table->relations_row_ids->at(0).size();
        new_relation->num_tuples = size;
        tuple_t * tuples = (tuple_t *) malloc(sizeof(tuple_t) * size);

        /* Initialize the tuple array */
        for (uint32_t i = 0; i < size; i++) {
            tuples[i].key     = values[row_ids[table_index][i]];
            tuples[i].payload = i;
        }

        new_relation->tuples = tuples;
    }
    else {
        /* Initialize relation */
        uint32_t size = table->relations_row_ids->at(0).size();
        new_relation->num_tuples = size;
        tuple_t * tuples = (tuple_t *) malloc(sizeof(tuple_t) * size);

        /* Initialize the tuple array */
        for (uint32_t i = 0; i < size; i++) {
            tuples[i].key     = values[i];
            tuples[i].payload = i;
        }

        new_relation->tuples = tuples;
    }

#ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    timeCreateTableT += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
#endif


    return new_relation;
}


int compare(const void * a, const void * b)
{
    return ( ((tuple_t*)a)->key - ((tuple_t*)b)->key );
}


table_t * Joiner::CreateTableT(result_t * result, table_t * table_r, table_t * table_s) {

#ifdef time
    struct timeval start;
    gettimeofday(&start, NULL);
#endif

    /* The num of relations for the two tables */
    const unsigned relnum_r = table_r->relations_bindings.size();
    const unsigned relnum_s = table_s->relations_bindings.size();

    /* Create - Initialize the new table_t */
    uint32_t num_relations = table_r->relations_bindings.size() + table_s->relations_bindings.size();
    table_t * new_table = new table_t;
    new_table->intermediate_res = true;
    new_table->column_j = new column_t;
    new_table->relations_row_ids = new matrix(num_relations);

    /* Allocate space for the row ids matrix */
    uint64_t  allocated_size = result->totalresults;
    for (size_t relation = 0; relation < num_relations; relation++) {
            new_table->relations_row_ids->at(relation).reserve(allocated_size);
    }

    new_table->relation_ids.reserve(table_r->relation_ids.size() + table_s->relation_ids.size());
    new_table->relation_ids.insert(new_table->relation_ids.end() ,table_r->relation_ids.begin(), table_r->relation_ids.end());
    new_table->relation_ids.insert(new_table->relation_ids.end() ,table_s->relation_ids.begin(), table_s->relation_ids.end());

    new_table->relations_bindings.reserve(num_relations);
    new_table->relations_bindings.insert(new_table->relations_bindings.end() ,table_r->relations_bindings.begin(), table_r->relations_bindings.end());
    new_table->relations_bindings.insert(new_table->relations_bindings.end() ,table_s->relations_bindings.begin(), table_s->relations_bindings.end());

    /* Get the 3 row_ids matrixes in referances */
    matrix & rids_res = *new_table->relations_row_ids;
    matrix & rids_r   = *table_r->relations_row_ids;
    matrix & rids_s   = *table_s->relations_row_ids;

    /* Get the chained buffer */
    /* TODO Make it possible for multi threading */
    chainedtuplebuffer_t * cb = (chainedtuplebuffer_t *) result->resultlist[0].results;

    /* Get the touples form the results */
    tuplebuffer_t * tb = cb->buf;
    uint32_t numbufs = cb->numbufs;
    uint32_t row_i;



    //qsort(tb->tuples, cb->writepos, sizeof(tuple_t), compare);

    /* Create table_t from tuples */
    for (uint32_t tup_i = 0; tup_i < cb->writepos; tup_i++) {
        row_i = tb->tuples[tup_i].key;
        for (uint32_t rel = 0; rel < relnum_r; rel++) {
            rids_res[rel].push_back( rids_r[rel][row_i] );
        }

        row_i = tb->tuples[tup_i].payload;
        for (uint32_t rel = 0; rel < relnum_s; rel++) {
            rids_res[relnum_r + rel].push_back( rids_s[rel][row_i] );
        }
    }

    /* --------------------------------------------------------------------------------------
    The N-1 buffer loops , where the num of tups are CHAINEDBUFF_NUMTUPLESPERBUF
    ---------------------------------------------------------------------------------------- */
    tb = tb->next;
    for (uint32_t buf_i = 0; buf_i < numbufs - 1; buf_i++) {
        /* Create table_t from tuples */
        for (uint32_t tup_i = 0; tup_i < CHAINEDBUFF_NUMTUPLESPERBUF; tup_i++) {
            row_i = tb->tuples[tup_i].key;
            for (uint32_t rel = 0; rel < relnum_r; rel++) {
                rids_res[rel].push_back( rids_r[rel][row_i] );
            }

            row_i = tb->tuples[tup_i].payload;
            for (uint32_t rel = 0; rel < relnum_s; rel++) {
                rids_res[relnum_r + rel].push_back( rids_s[rel][row_i] );
            }
        }

        /* Go the the next buffer */
        tb = tb->next;
    }

#ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    timeCreateTableT += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
#endif

    return new_table;
}

/* =================================== */

/* +---------------------+
   |The joiner functions |
   +---------------------+ */

/* Its better not to use it TODO change it */
void Joiner::Select(FilterInfo &fil_info, table_t* table) {

#ifdef time
    struct timeval start;
    gettimeofday(&start, NULL);
#endif

    /* Construct table  - Initialize variable */
    (table->intermediate_res)? (construct(table)) : ((void)0);
    SelectInfo &sel_info = fil_info.filterColumn;
    uint64_t filter = fil_info.constant;

    if (fil_info.comparison == FilterInfo::Comparison::Less) {
        SelectLess(table, filter);
    } else if (fil_info.comparison == FilterInfo::Comparison::Greater) {
        SelectGreater(table, filter);
    } else if (fil_info.comparison == FilterInfo::Comparison::Equal) {
        SelectEqual(table, filter);
    }

#ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    timeSelectFilter += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
#endif

}

void Joiner::SelectEqual(table_t *table, int filter) {
    /* Initialize helping variables */
    uint64_t *const values  = table->column_j->values;
    int table_index         = table->column_j->table_index;
    const uint64_t rel_num  = table->relations_row_ids->size();

    matrix & old_row_ids = *table->relations_row_ids;
    const uint64_t size  = old_row_ids[table_index].size();
    matrix * new_row_ids = new matrix(rel_num);
    new_row_ids->at(0).reserve(size/2);

    /* Update the row ids of the table */
    for (size_t index = 0; index < size; index++) {
        if (values[index] == filter) {
            for (size_t rel_index = 0; rel_index < rel_num; rel_index++) {
                new_row_ids->operator[](rel_index).push_back(old_row_ids[rel_index][index]);
            }
        }
    }

    /* Swap the old vector with the new one */
    delete table->relations_row_ids;
    table->relations_row_ids = new_row_ids;
    table->intermediate_res = true;
}

void Joiner::SelectGreater(table_t *table, int filter){
    /* Initialize helping variables */
    uint64_t *const values  = table->column_j->values;
    int table_index         = table->column_j->table_index;
    const uint64_t rel_num  = table->relations_row_ids->size();

    matrix & old_row_ids = *table->relations_row_ids;
    const uint64_t size  = old_row_ids[table_index].size();
    matrix * new_row_ids = new matrix(rel_num);
    new_row_ids->at(0).reserve(size/2);

    /* Update the row ids of the table */
    for (size_t index = 0; index < size; index++) {
        if (values[index] > filter) {
            for (size_t rel_index = 0; rel_index < rel_num; rel_index++) {
                new_row_ids->operator[](rel_index).push_back(old_row_ids[rel_index][index]);
            }
        }
    }

    /* Swap the old vector with the new one */
    delete table->relations_row_ids;
    table->relations_row_ids = new_row_ids;
    table->intermediate_res = true;
}

void Joiner::SelectLess(table_t *table, int filter){
    /* Initialize helping variables */
    uint64_t *const values  = table->column_j->values;
    int table_index         = table->column_j->table_index;
    const uint64_t rel_num  = table->relations_row_ids->size();

    matrix & old_row_ids = *table->relations_row_ids;
    const uint64_t size  = old_row_ids[table_index].size();
    matrix * new_row_ids = new matrix(rel_num);
    new_row_ids->at(0).reserve(size/2);

    /* Update the row ids of the table */
    for (size_t index = 0; index < size; index++) {
        if (values[index] < filter) {
            for (size_t rel_index = 0; rel_index < rel_num; rel_index++) {
                new_row_ids->operator[](rel_index).push_back(old_row_ids[rel_index][index]);
            }
        }
    }

    /* Swap the old vector with the new one */
    delete table->relations_row_ids;
    table->relations_row_ids = new_row_ids;
    table->intermediate_res = true;
}

void Joiner::AddColumnToTableT(SelectInfo &sel_info, table_t *table) {

#ifdef time
    struct timeval start;
    gettimeofday(&start, NULL);
#endif

    /* Create a new column_t for table */
    column_t &column = *table->column_j;
    std::vector<unsigned> &relation_mapping = table->relations_bindings;

    /* Get the relation from joiner */
    Relation &rel = getRelation(sel_info.relId);
    column.size   = rel.size;
    column.values = rel.columns.at(sel_info.colId);
    column.id     = sel_info.colId;
    column.table_index = -1;
    unsigned relation_binding = sel_info.binding;


    /* Get the right index from the relation id table */
    for (size_t index = 0; index < relation_mapping.size(); index++) {
        if (relation_mapping[index] == relation_binding){
            column.table_index = index;
            break;
        }
    }

    /* Error msg for debuging */
    if (column.table_index == -1)
        std::cerr << "At AddColumnToTableT, Id not matchin with intermediate result vectors" << '\n';

#ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    timeAddColumn += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
#endif

}

table_t* Joiner::CreateTableTFromId(unsigned rel_id, unsigned rel_binding) {

#ifdef time
    struct timeval start;
    gettimeofday(&start, NULL);
#endif

    /* Get realtion */
    Relation & rel = getRelation(rel_id);

    /* Crate - Initialize a table_t */
    table_t *const table_t_ptr = new table_t;
    table_t_ptr->column_j = new column_t;
    table_t_ptr->intermediate_res = false;
    table_t_ptr->relations_row_ids = new matrix(1, j_vector(rel.size));
    matrix & rel_row_ids = *table_t_ptr->relations_row_ids;

    /* Create the relations_row_ids and relation_ids vectors */
    uint64_t rel_size  = rel.size;
    for (size_t i = 0; i < rel_size; i++) {
        rel_row_ids[0][i] = i;
    }

    /* Keep a mapping with the rowids table and the relaito ids na bindings */
    table_t_ptr->relation_ids.push_back(rel_id);
    table_t_ptr->relations_bindings.push_back(rel_binding);

#ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    timeCreateTable += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
#endif

    return table_t_ptr;
}

table_t* Joiner::join(table_t *table_r, table_t *table_s, PredicateInfo &pred_info) {

    relation_t * r1 = CreateRelationT(table_r, pred_info.left);
    relation_t * r2 = CreateRelationT(table_s, pred_info.right);

#ifdef TIME_DETAILS
    struct timeval start, end;
    gettimeofday(&start, NULL);
#endif
    result_t * res  = RJ(r1, r2, 0);
#ifdef TIME_DETAILS
    gettimeofday(&end, NULL);
    double dt = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    // std::ostringstream strs;
    if(!done_testing) {
        cerr << "RJ: " << dt << " sec" << endl;
        flush(cerr);
        // timeDetStr.append(strs.str());
    }
#endif
#ifdef TIME_DETAILS
    gettimeofday(&start, NULL);
#endif
    table_t *temp = CreateTableT(res, table_r, table_s);
#ifdef TIME_DETAILS
    gettimeofday(&end, NULL);
    dt = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    if(!done_testing) {
        cerr << "CreateTableT: " << dt << "sec for " << res->totalresults << " results" << endl;
        flush(cerr);
    }
#endif
    return temp;

    /* Construct the tables in case of intermediate results */
    //(table_r->intermediate_res)? (construct(table_r)) : ((void)0);
    //(table_s->intermediate_res)? (construct(table_s)) : ((void)0);
    /* Join the columns */
    //return low_join(table_r, table_s);
}

// for_2 join UNOPTIMIZED
uint64_t Joiner::for_2(table_t* table_r, table_t* table_s) {
    uint64_t check_sum = 0;
    /* create hash_table for the hash_join phase */
    std::unordered_multimap<uint64_t, hash_entry> hash_c;
    /* hash_size->size of the hashtable,iter_size->size to iterate over to find same vals */
    uint64_t hash_size, iter_size;
    column_t *hash_col, *iter_col;

    /* check on wich table will create the hash_table */
    if (table_r->column_j->size <= table_s->column_j->size) {
        hash_size = table_r->column_j->size;
        hash_col = table_r->column_j;
        matrix &h_rows = *table_r->relations_row_ids;

        iter_size = table_s->column_j->size;
        iter_col = table_s->column_j;
        matrix &i_rows = *table_s->relations_row_ids;

        /* now put the values of the column_r in the hash_table(construction phase) */
        for (uint64_t i = 0; i < hash_size; i++) {
            /* store hash[value of the column] = {rowid, index} */
            hash_entry hs;
            hs.row_id = h_rows[hash_col->table_index][i];
            hs.index = i;
            hash_c.insert({hash_col->values[i], hs});
        }
        /* now the phase of hashing */
        for (uint64_t i = 0; i < iter_size; i++) {
            /* remember we may have multi vals in 1 key,if it isnt a primary key */
            /* vals->first = key ,vals->second = value */
            auto range_vals = hash_c.equal_range(iter_col->values[i]);
            for(auto &vals = range_vals.first; vals != range_vals.second; vals++) {
                check_sum += iter_col->values[i];
            }
        }
    }
    /* table_r->column_j->size > table_s->column_j->size */
    else {
        hash_size = table_s->column_j->size;
        hash_col = table_s->column_j;
        matrix &h_rows = *table_s->relations_row_ids;

        iter_size = table_r->column_j->size;
        iter_col = table_r->column_j;
        matrix &i_rows = *table_r->relations_row_ids;

        /* now put the values of the column_r in the hash_table(construction phase) */
        for (uint64_t i = 0; i < hash_size; i++) {
            /* store hash[value of the column] = {rowid, index} */
            hash_entry hs;
            hs.row_id = h_rows[hash_col->table_index][i];
            hs.index = i;
            hash_c.insert({hash_col->values[i], hs});
        }
        /* now the phase of hashing */
        for (uint64_t i = 0; i < iter_size; i++) {
            /* remember we may have multi vals in 1 key,if it isnt a primary key */
            /* vals->first = key ,vals->second = value */
            auto range_vals = hash_c.equal_range(iter_col->values[i]);
            for(auto &vals = range_vals.first; vals != range_vals.second; vals++) {
                check_sum += iter_col->values[i];
            }
        }
    }
    /* do the cleaning */
    delete table_r->relations_row_ids;
    delete table_s->relations_row_ids;

    return check_sum;
}

/* The self Join Function */
table_t * Joiner::SelfJoin(table_t *table, PredicateInfo *predicate_ptr) {

#ifdef time
    struct timeval start;
    gettimeofday(&start, NULL);
#endif

    /* Create - Initialize a new table */
    table_t *new_table            = new table_t;
    new_table->relation_ids       = std::vector<unsigned>(table->relation_ids);
    new_table->relations_bindings = std::vector<unsigned>(table->relations_bindings);
    new_table->relations_row_ids  = new matrix;
    new_table->intermediate_res   = true;
    new_table->column_j           = new column_t;

    /* Get the 2 relation rows ids vectors in referances */
    matrix &row_ids_matrix       = *(table->relations_row_ids);
    matrix &new_row_ids_matrix   = *(new_table->relations_row_ids);

    /* Get the 2 relations */
    Relation & relation_l        = getRelation(predicate_ptr->left.relId);
    Relation & relation_r        = getRelation(predicate_ptr->right.relId);

    /* Get their columns */
    uint64_t *column_values_l    = relation_l.columns[predicate_ptr->left.colId];
    uint64_t *column_values_r    = relation_r.columns[predicate_ptr->right.colId];

    /* Get their column's sizes */
    int column_size_l            = relation_l.size;
    int column_size_r            = relation_r.size;

    /* Fint the indexes of the raltions in the table's */
    int index_l                  = -1;
    int index_r                  = -1;
    int relations_num            = table->relations_bindings.size();

    for (ssize_t index = 0; index < relations_num ; index++) {

        if (predicate_ptr->left.binding == table->relations_bindings[index]) {
            index_l = index;
        }
        if (predicate_ptr->right.binding == table->relations_bindings[index]){
            index_r = index;
        }

        /* Initialize the new matrix */
        new_row_ids_matrix.push_back(j_vector());
    }

#ifdef com
    if (index_l == -1 || index_r == -1) std::cerr << "Error in SelfJoin: No mapping found for predicates" << '\n';
#endif

    /* Loop all the row_ids and keep the one's matching the predicate */
    int rows_number = table->relations_row_ids->operator[](0).size();
    for (ssize_t i = 0; i < rows_number; i++) {

        /* Apply the predicate: In case of success add to new table */
        if (column_values_l[row_ids_matrix[index_l][i]] == column_values_r[row_ids_matrix[index_r][i]]) {

            /* Add this row_id to all the relations */
            for (ssize_t relation = 0; relation < relations_num; relation++) {
                new_row_ids_matrix[relation].push_back(row_ids_matrix[relation][i]);
            }
        }
    }

#ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    timeSelfJoin += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
#endif

    /*Delete old table_t */
    //delete table->relations_row_ids;

    return new_table;
}


/*
 * 1)Classic hash_join implementation with unorderd_map(stl)
 * 2)Create hashtable from the row_table with the lowest size
 * 3)Ids E [0,...,size-1]
 * 4)Made the code repeatable to put some & in the arrays of row ids
*/
table_t* Joiner::low_join(table_t *table_r, table_t *table_s) {

#ifdef time
    struct timeval start;
    gettimeofday(&start, NULL);
#endif

    /* create hash_table for the hash_join phase */
    std::unordered_multimap<uint64_t, hash_entry> hash_c;

    /* the new table_t to continue the joins */
    table_t *updated_table_t = new table_t;


    /* hash_size->size of the hashtable,iter_size->size to iterate over to find same vals */
    uint64_t hash_size,iter_size;
    column_t *hash_col;
    column_t *iter_col;


    /* check on wich table will create the hash_table */
    if (table_r->column_j->size <= table_s->column_j->size) {
        hash_size = table_r->column_j->size;
        hash_col = table_r->column_j;
        matrix &h_rows = *table_r->relations_row_ids;

        iter_size = table_s->column_j->size;
        iter_col = table_s->column_j;
        matrix &i_rows = *table_s->relations_row_ids;

#ifdef time
        struct timeval start_build;
        gettimeofday(&start_build, NULL);
#endif

        /* now put the values of the column_r in the hash_table(construction phase) */
        for (uint64_t i = 0; i < hash_size; i++) {
            /* store hash[value of the column] = {rowid, index} */
            hash_entry hs;
            hs.row_id = h_rows[hash_col->table_index][i];
            hs.index = i;
            hash_c.insert({hash_col->values[i], hs});
        }


#ifdef time
        struct timeval end_build;
        gettimeofday(&end_build, NULL);
        timeBuildPhase += (end_build.tv_sec - start_build.tv_sec) + (end_build.tv_usec - start_build.tv_usec) / 1000000.0;

        struct timeval start_probe;
        gettimeofday(&start_probe, NULL);
#endif
        /* create the updated relations_row_ids, merge the sizes*/
        updated_table_t->relations_row_ids = new matrix(h_rows.size()+i_rows.size());
        uint64_t  allocated_size = (hash_size < iter_size ) ? (uint64_t)(hash_size) : (uint64_t)(iter_size);
        for (size_t relation = 0; relation < h_rows.size()+i_rows.size(); relation++) {
                updated_table_t->relations_row_ids->operator[](relation).reserve(allocated_size);
        }
        matrix &update_row_ids = *updated_table_t->relations_row_ids;

        /* now the phase of hashing */
        for (uint64_t i = 0; i < iter_size; i++) {
            /* remember we may have multi vals in 1 key,if it isnt a primary key */
            /* vals->first = key ,vals->second = value */
            auto range_vals = hash_c.equal_range(iter_col->values[i]);
            for(auto &vals = range_vals.first; vals != range_vals.second; vals++) {
                /* store all the result then push it int the new row ids */
                /* its faster than to push back 1 every time */
                /* get the first values from the r's rows ids */
                for (uint64_t j = 0 ; j < h_rows.size(); j++)
                    update_row_ids[j].emplace_back(h_rows[j][vals->second.index]);

                /* then go to the s's row ids to get the values */
                for (uint64_t j = 0; j < i_rows.size(); j++)
                    update_row_ids[j + h_rows.size()].emplace_back(i_rows[j][i]);
            }
        }

        updated_table_t->relation_ids.reserve(table_r->relation_ids.size()+table_s->relation_ids.size());
        updated_table_t->relation_ids.insert(updated_table_t->relation_ids.end() ,table_r->relation_ids.begin(), table_r->relation_ids.end());
        updated_table_t->relation_ids.insert(updated_table_t->relation_ids.end() ,table_s->relation_ids.begin(), table_s->relation_ids.end());

        updated_table_t->relations_bindings.reserve(table_r->relations_bindings.size()+table_s->relations_bindings.size());
        updated_table_t->relations_bindings.insert(updated_table_t->relations_bindings.end() ,table_r->relations_bindings.begin(), table_r->relations_bindings.end());
        updated_table_t->relations_bindings.insert(updated_table_t->relations_bindings.end() ,table_s->relations_bindings.begin(), table_s->relations_bindings.end());
#ifdef time
        struct timeval end_probe;
        gettimeofday(&end_probe, NULL);
        timeProbePhase += (end_probe.tv_sec - start_probe.tv_sec) + (end_probe.tv_usec - start_probe.tv_usec) / 1000000.0;
#endif
    }
    /* table_r->column_j->size > table_s->column_j->size */
    else {

#ifdef time
        struct timeval start_build;
        gettimeofday(&start_build, NULL);
#endif
        hash_size = table_s->column_j->size;
        hash_col = table_s->column_j;
        matrix &h_rows = *table_s->relations_row_ids;

        iter_size = table_r->column_j->size;
        iter_col = table_r->column_j;
        matrix &i_rows = *table_r->relations_row_ids;

        /* now put the values of the column_r in the hash_table(construction phase) */
        for (uint64_t i = 0; i < hash_size; i++) {
            /* store hash[value of the column] = {rowid, index} */
            hash_entry hs;
            hs.row_id = h_rows[hash_col->table_index][i];
            hs.index = i;
            hash_c.insert({hash_col->values[i], hs});
        }
#ifdef time
        struct timeval end_build;
        gettimeofday(&end_build, NULL);
        timeBuildPhase += (end_build.tv_sec - start_build.tv_sec) + (end_build.tv_usec - start_build.tv_usec) / 1000000.0;

        struct timeval start_probe;
        gettimeofday(&start_probe, NULL);
#endif
        /* create the updated relations_row_ids, merge the sizes*/
        updated_table_t->relations_row_ids = new matrix(h_rows.size()+i_rows.size());
        uint64_t  allocated_size = (hash_size < iter_size ) ? (uint64_t)(hash_size) : (uint64_t)(iter_size);
        for (size_t relation = 0; relation < h_rows.size()+i_rows.size(); relation++) {
                updated_table_t->relations_row_ids->operator[](relation).reserve(allocated_size);
        }
        //updated_table_t->relations_row_ids->resize(h_rows.size()+i_rows.size(), std::vector<uint64_t>());
        matrix &update_row_ids = *updated_table_t->relations_row_ids;

        /* now the phase of hashing */
        for (uint64_t i = 0; i < iter_size; i++) {
            /* remember we may have multi vals in 1 key,if it isnt a primary key */
            /* vals->first = key ,vals->second = value */
            auto range_vals = hash_c.equal_range(iter_col->values[i]);
            for(auto &vals = range_vals.first; vals != range_vals.second; vals++) {
                /* store all the result then push it int the new row ids */
                /* its faster than to push back 1 every time */

                for (uint64_t j = 0 ; j < h_rows.size(); j++)
                    update_row_ids[j].emplace_back(h_rows[j][vals->second.index]);

                /* then go to the s's row ids to get the values */
                for (uint64_t j = 0; j < i_rows.size(); j++)
                    update_row_ids[j + h_rows.size()].emplace_back(i_rows[j][i]);
            }
        }
        updated_table_t->relation_ids.reserve(table_s->relation_ids.size()+table_r->relation_ids.size());
        updated_table_t->relation_ids.insert(updated_table_t->relation_ids.end() ,table_s->relation_ids.begin(), table_s->relation_ids.end());
        updated_table_t->relation_ids.insert(updated_table_t->relation_ids.end() ,table_r->relation_ids.begin(), table_r->relation_ids.end());

        updated_table_t->relations_bindings.reserve(table_s->relations_bindings.size()+table_r->relations_bindings.size());
        updated_table_t->relations_bindings.insert(updated_table_t->relations_bindings.end() ,table_s->relations_bindings.begin(), table_s->relations_bindings.end());
        updated_table_t->relations_bindings.insert(updated_table_t->relations_bindings.end() ,table_r->relations_bindings.begin(), table_r->relations_bindings.end());

#ifdef time
        struct timeval end_probe;
        gettimeofday(&end_probe, NULL);
        timeProbePhase += (end_probe.tv_sec - start_probe.tv_sec) + (end_probe.tv_usec - start_probe.tv_usec) / 1000000.0;
#endif

    }
    /* concatenate the relaitons ids for the merge */
    updated_table_t->intermediate_res = true;
    updated_table_t->column_j = new column_t;

    /* do the cleaning */
    delete table_r->relations_row_ids;
    delete table_s->relations_row_ids;

#ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    timeLowJoin += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
#endif

    return updated_table_t;
}

void Joiner::construct(table_t *table) {
#ifdef time
    struct timeval start;
    gettimeofday(&start, NULL);
#endif

    /* Innitilize helping variables */
    column_t &column = *table->column_j;
    const uint64_t *column_values = column.values;
    const int       table_index   = column.table_index;
    const uint64_t  column_size   = table->relations_row_ids->operator[](table_index).size();
    matrix &row_ids = *table->relations_row_ids;

    /* Create a new value's array  */
    uint64_t *const new_values  = new uint64_t[column_size];

    /* construct a new array with the row ids that it's sorted */
    //vector<int> rids = row_ids[table_index];  //cp construct
    //std::sort(rids.begin(), rids.end());

    /* Pass the values of the old column to the new one, based on the row ids of the joiner */
    for (int i = 0; i < column_size; i++) {
    	new_values[i] = column_values[row_ids[table_index][i]];//rids[i]];
    }

    /* Update the column of the table */
    column.values = new_values;
    column.size   = column_size;

#ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    timeConstruct += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
#endif
}

struct threadSumArg_t {
    uint32_t low;
    uint32_t high;
    const uint64_t * array;
};

// uint64_t threadSum(const uint64_t *array, uint32_t low, uint32_t high) {
//
//     uint64_t res;
//     for (uint32_t idx = low; idx < high; idx++) {
//         res += array[idx];
//     }
//
//     return res;
// }


void *threadSum(void * arg) {
    struct threadSumArg_t * sumArg = (struct threadSumArg_t *) arg;

    // Loop the array for the checksum
    uint64_t * res  = (uint64_t*) malloc(sizeof(uint64_t)); *res = 0;
    uint32_t low  = sumArg->low;
    uint32_t high = sumArg->high;
    const uint64_t * array = sumArg->array;
    for (uint32_t idx = low; idx <= high; idx++) {
        *res += array[idx];
    }

    // Free the argument
    free(sumArg);

    return (void *)res;
}

//CHECK SUM FUNCTION
std::string Joiner::check_sum(SelectInfo &sel_info, table_t *table) {

    /* to create the final cehcksum column */
    AddColumnToTableT(sel_info, table);
    construct(table);

    uint64_t* col = table->column_j->values;
    const uint64_t size = table->column_j->size;
    uint64_t sum = 0;

    if (size == 0) {
        return "NULL";
    }
    else if (size < 1000) {
        for (uint64_t i = 0 ; i < size; i++)
            sum += col[i];
        return to_string(sum);
    }
    else {
        // 4 thread array
        pthread_t threads[THREAD_NUM];

        // Creating THREAD_NUM threads
        for (int i = 0; i < THREAD_NUM; i++) {
            struct threadSumArg_t * arg = (struct threadSumArg_t *) malloc(sizeof(struct threadSumArg_t));
            arg->low   = (i == 0) ? 0 : size/THREAD_NUM*i + size%THREAD_NUM;
            arg->high  = (i == 0) ? size/THREAD_NUM + size%THREAD_NUM - 1 : arg->low + size/THREAD_NUM - 1;
            arg->array = col;
            pthread_create(&threads[i], NULL, threadSum, (void*)arg);
        }

        // joining THREAD_NUM threads i.e. waiting for all THREAD_NUM threads to complete
        uint64_t * res;
        int threads_joined = 0;
        while (threads_joined != THREAD_NUM){
            for (size_t i = 0; i < THREAD_NUM; i++) {
                if (pthread_tryjoin_np(threads[i], (void**)&res) > 0) {
                    sum += *res;
                    free(res);
                    threads_joined++;
                }
            }
        }

        return to_string(sum);
    }

}

// OLD check_sum
// string Joiner::check_sum(SelectInfo &sel_info, table_t *table,
//                          threadpool11::Pool & pool, std::array<std::future<uint64_t>, THREAD_NUM> & futures) {
//
//     /* to create the final cehcksum column */
//     AddColumnToTableT(sel_info, table);
//     construct(table);
//
//     const uint64_t* col = table->column_j->values;
//     const uint64_t size = table->column_j->size;
//     uint64_t sum = 0;
//
//     for (int i = 0; i < THREAD_NUM; ++i) {
//         uint32_t low  = (i == 0) ? size/4*i + size%4 : size/4*i + 1;
//         uint32_t high = low + size/4;
//         futures[i] = pool.postWork<uint64_t>([=]() { return threadSum(col, low, high); });
//     }
//
//     for (auto& it : futures) {
//         sum += it.get();
//     }
//
//     for (uint64_t i = 0 ; i < size; i++)
//         sum += col[i];
//
//         switch (sum) {
//             case 0  :  return "NULL";
//             default :  return to_string(sum);
//     }
// }

// Loads a relation from disk
void Joiner::addRelation(const char* fileName) {
    relations.emplace_back(fileName);
}

// Loads a relation from disk
Relation& Joiner::getRelation(unsigned relationId) {
    if (relationId >= relations.size()) {
        cerr << "Relation with id: " << relationId << " does not exist" << endl;
        throw;
    }
    return relations[relationId];
}

// Get the total number of relations
int Joiner::getRelationsCount() {
    return relations.size();
}

// Hashes a value and returns a check-sum
// The check should be NULL if there is no qualifying tuple
void Joiner::join(QueryInfo& i) {}

/* +---------------------+
   |The Column functions |
   +---------------------+ */

void PrintColumn(column_t *column) {
    /* Print the column's table id */
    std::cerr << "Column of table " << column->id << " and size " << column->size << '\n';

    /* Iterate over the column's values and print them */
    for (int i = 0; i < column->size; i++) {
        std::cerr << column->values[i] << '\n';
    }
}

int main(int argc, char* argv[]) {
    Joiner joiner;

    // Read join relations
    string line;
    while (getline(cin, line)) {
        if (line == "Done") break;
        joiner.addRelation(line.c_str());
    }

    // Preparation phase (not timed)
    QueryPlan queryPlan;

    // Get the needed info of every column
    queryPlan.fillColumnInfo(joiner);

#ifdef TIME_DETAILS
    done_testing = false;
    cerr << endl;
    // bool its_over = false;
    for (int i = 1000; i <= 41000; i += 10000) {
        for (int j = 1000; j <= 41000; j += 10000) {
            vector<table_t*> tables;
            tables.push_back(joiner.CreateTableTFromId(7, 7));
            tables.push_back(joiner.CreateTableTFromId(13, 13));
            if (i <= tables[0]->relations_row_ids[0][0].size()) {
                for (int k = 0; k < tables[0]->relations_row_ids[0].size(); k++)
                    tables[0]->relations_row_ids[k][0].resize(i);
            } else {
                tables[0] = NULL;
            }
            if (j <= tables[1]->relations_row_ids[0][0].size()) {
                for (int k = 0; k < tables[1]->relations_row_ids[0].size(); k++)
                    tables[1]->relations_row_ids[k][0].resize(j);
            } else {
                tables[1] = NULL;
            }

            if (!tables[0] || !tables[1]) {
                // its_over = true;
                break;
            }

            PredicateInfo predicate;
            // predicate.left.relId = 13;
            // predicate.left.binding = 0;
            // predicate.left.colId = 1;
            // predicate.right.relId = 13;
            // predicate.right.binding = 1;
            // predicate.right.colId = 1;
            struct timeval start, end;
            gettimeofday(&start, NULL);
            // joiner.join(tables[0], tables[0], predicate);
            gettimeofday(&end, NULL);
            double dt = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
            // std::ostringstream strs;
            // cerr << "13,\t" << i << ",\t13,\t" << j << ",\t" << dt << " sec" << endl;
            // flush(cerr);
            // timeDetStr.append(strs.str());

            predicate.left.relId = 7;
            predicate.left.binding = 0;
            predicate.left.colId = 1;
            predicate.right.relId = 13;
            predicate.right.binding = 1;
            predicate.right.colId = 1;
            gettimeofday(&start, NULL);
            joiner.join(tables[0], tables[1], predicate);
            gettimeofday(&end, NULL);
            dt = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
            cerr << "7,\t" << i << ",\t13,\t" << j << ",\t" << dt << "sec" << endl;
        }
        // if (its_over)
        //     break;
    }
    cerr << timeDetStr << endl;
    done_testing = true;
#endif
    #ifdef time
    struct timeval end;
    gettimeofday(&end, NULL);
    //timePreparation += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    #endif


    // Thread pool Initialize
    //threadpool11::Pool pool;  // Create a threadPool
    //std::array<std::future<uint64_t>, THREAD_NUM> futures;
    //pool.setWorkerCount(std::thread::hardware_concurrency());

    // The test harness will send the first query after 1 second.
    QueryInfo i;
    int q_counter = 0;
    while (getline(cin, line)) {
        if (line == "F") continue; // End of a batch

        // Parse the query
        //std::cerr << "Q " << q_counter  << ":" << line << '\n';
        i.parseQuery(line);
        cleanQuery(i);
        //q_counter++;

        #ifdef time
        struct timeval start;
        gettimeofday(&start, NULL);
        #endif

        // JTree *jTreePtr = treegen(&i);
        // Create the optimal join tree
        JoinTree* optimalJoinTree = queryPlan.joinTreePtr->build(i, queryPlan.columnInfos);

        #ifdef time
        gettimeofday(&end, NULL);
        timeTreegen += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        #endif

        // int *plan = NULL, plan_size = 0;
        // table_t * result =  jTreeMakePlan(jTreePtr, joiner, plan);
        table_t *result = optimalJoinTree->root->execute(optimalJoinTree->root, joiner, i);

        #ifdef time
        gettimeofday(&start, NULL);
        #endif

        string result_str;
        uint64_t checksum = 0;
        unordered_map<string, string> cached_sums;
        vector<SelectInfo> &selections = i.selections;
        for (size_t i = 0; i < selections.size(); i++) {

            /* Check if checksum is cached */
            string key = to_string(selections[i].binding) + to_string(selections[i].colId);
            unordered_map<string, string>::const_iterator got = cached_sums.find(key);
            if (got != cached_sums.end()) {
                result_str += got->second;
            } else {
                //string str = joiner.check_sum(selections[i], result, pool, futures);
                string str = joiner.check_sum(selections[i], result);
                cached_sums.insert(make_pair(key, str));
                result_str += str;
            }

            /* Create the write check sum */
            if (i != selections.size() - 1) {
                result_str +=  " ";
            }
        }

        #ifdef time
        gettimeofday(&end, NULL);
        timeCheckSum += (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        #endif

        // Print the result
        std::cout << result_str << endl;
}

#ifdef time
    std::cerr << "timeCreateTableT: " << (long)(timeCreateTableT * 1000) << endl;
    std::cerr << "timeCreateRelationT: " << (long)(timeCreateRelationT * 1000) << endl;
    std::cerr << "timeConstruct: " << (long)(timeConstruct * 1000) << endl;
    std::cerr << "timeSelectFilter: " << (long)(timeSelectFilter * 1000) << endl;
    std::cerr << "timeSelfJoin: " << (long)(timeSelfJoin * 1000) << endl;
    std::cerr << "timeRadixJoin: " << (long)(timeRadixJoin * 1000) << endl;
    //std::cerr << "->timeBuildPhase: " << (long)(timeBuildPhase * 1000) << endl;
    //std::cerr << "->timeProbePhase: " << (long)(timeProbePhase * 1000) << endl;
    std::cerr << "timeAddColumn: " << (long)(timeAddColumn * 1000) << endl;
    std::cerr << "timeCreateTable: " << (long)(timeCreateTable * 1000) << endl;
    std::cerr << "timeTreegen: " << (long)(timeTreegen * 1000) << endl;
    std::cerr << "timeCheckSum: " << (long)(timeCheckSum * 1000) << endl;
    flush(std::cerr);
#endif

    return 0;
}
