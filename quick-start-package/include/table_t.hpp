#pragma once

#include <vector>
#include <unordered_map>

typedef std::vector<std::vector<int>> matrix;
typedef std::vector<int> j_vector;
typedef struct table_t table_t;
typedef struct column_t column_t;
typedef struct hash_entry hash_entry;
typedef struct cartesian_product cartesian_product_t;

struct hash_entry {
    uint64_t row_id;
    uint64_t index;
};

struct column_t {
    uint64_t *values;
    uint64_t  size;
    uint64_t  table_index;
    unsigned  id;
};

struct table_t {

    /* Row Ids and relation Ids */
    unsigned * row_ids;
    unsigned    tups_num;
    unsigned    rels_num;

    /* use the binfing to map the relations */
    std::unordered_map<unsigned, unsigned> relations_bindings;

    /* Intermediate result falg */
    bool intermediate_res;

    /* column_t pointer of column to join */
    column_t *column_j;
};
