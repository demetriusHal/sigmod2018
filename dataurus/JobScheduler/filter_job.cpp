#include "filter_job.h"



// Give a good range depending on threads and size
int getRange(int threads, unsigned size) {
     // if (size < 10000)
     //     return threads/2;

    return threads;
}


// ------- BIT MAP FILTERING -----------
int JobBitMapNonInterLessFiter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[i] < args_.filter) {
            args_.size++;
            args_.bitmap[i] = true;
        }
    }
}
int JobBitMapNonInterGreaterFiter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[i] > args_.filter) {
            args_.size++;
            args_.bitmap[i] = true;
        }
    }
}
int JobBitMapNonInterEqualFiter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[i] == args_.filter) {
            args_.size++;
            args_.bitmap[i] = true;
        }
    }
}
// - - - - - Intermediate functions - - - - - -
int JobBitMapInterLessFiter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        // check ti bitmap first
        if (args_.bitmap[i]) {
            if (args_.values[i] < args_.filter) {
                args_.size++;
            }
            else {
                args_.bitmap[i] = false;   // change the bitmap
            }
        }
    }
}
int JobBitMapInterGreaterFiter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        // check ti bitmap first
        if (args_.bitmap[i]) {
            if (args_.values[i] > args_.filter) {
                args_.size++;
            }
            else {
                args_.bitmap[i] = false;   // change the bitmap
            }
        }
    }
}
int JobBitMapInterEqualFiter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        // check ti bitmap first
        if (args_.bitmap[i]) {
            if (args_.values[i] == args_.filter) {
                args_.size++;
            }
            else {
                args_.bitmap[i] = false;   // change the bitmap
            }
        }
    }
}
//-----------------------------



// Self Join functions
int JobSelfJoinFindSize::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.column_values_l[args_.row_ids_matrix[i*(args_.rels_number) + args_.index_l]] == args_.column_values_r[args_.row_ids_matrix[i*(args_.rels_number) + args_.index_r]])
            args_.size++;
    }
}

int JobSelfJoin::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        /* Apply the predicate: In case of success add to new table */
        if (args_.column_values_l[args_.row_ids_matrix[i*(args_.rels_number) + args_.index_l]] == args_.column_values_r[args_.row_ids_matrix[i*(args_.rels_number) + args_.index_r]]) {
            /* Add this row_id to all the relations */
            for (size_t relation = 0; relation < args_.rels_number; relation++) {
                args_.new_row_ids_matrix[(args_.prefix)*(args_.rels_number) + relation] = args_.row_ids_matrix[i*(args_.rels_number) + relation];
            }
            args_.prefix++;
        }
    }
    // free(&args_);
}


// Less Intermediate Filter functions
int JobLessInterFindSize::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[args_.old_rids[i*args_.rel_num + args_.table_index]] < args_.filter)
            args_.size++;
    }
}

int JobLessInterFilter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[args_.old_rids[i*args_.rel_num + args_.table_index]] < args_.filter) {
            for (size_t j = 0; j < args_.rel_num; j++){
                args_.new_array[args_.prefix*args_.rel_num + j] = args_.old_rids[i*args_.rel_num + j];
            }
            args_.prefix++;
        }
    }

    // free(&args_);
}

// Greater Intermediate Filter functions
int JobGreaterInterFindSize::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[args_.old_rids[i*args_.rel_num + args_.table_index]] > args_.filter)
            args_.size++;
    }
}

int JobGreaterInterFilter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[args_.old_rids[i*args_.rel_num + args_.table_index]] > args_.filter){
            for (size_t j = 0; j < args_.rel_num; j++){
                args_.new_array[args_.prefix*args_.rel_num + j] = args_.old_rids[i*args_.rel_num + j];
            }
            args_.prefix++;
        }
    }

    // free(&args_);
}

// Equal Intermediate Filter functions
int JobEqualInterFindSize::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[args_.old_rids[i*args_.rel_num + args_.table_index]] == args_.filter)
            args_.size++;
    }
}

int JobEqualInterFilter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[args_.old_rids[i*args_.rel_num + args_.table_index]] == args_.filter) {
            for (size_t j = 0; j < args_.rel_num; j++){
                args_.new_array[args_.prefix*args_.rel_num + j] = args_.old_rids[i*args_.rel_num + j];
            }
            args_.prefix++;
        }
    }

    // free(&args_);
}


/*++++++++++++++++++++++++++++++++++++*/
/* NON INTERMERIATE PRALLEL FUNCTIONS */
/*++++++++++++++++++++++++++++++++++++*/

// All filter Run functions
int JobAllNonInterFindSize::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {

        /* Loop for all the predicates */
        /* Loop for all the predicates */
        bool pass;
        for (auto filter : (*args_.filterPtrs)) {
            pass = false;

            /* If it passes all the filter */
            if ((*filter).comparison == FilterInfo::Comparison::Less)
                pass = (*args_.columns)[(*filter).filterColumn.colId][i] < (*filter).constant ? true : false;
            else if ((*filter).comparison == FilterInfo::Comparison::Greater)
                pass = (*args_.columns)[(*filter).filterColumn.colId][i] > (*filter).constant? true : false;
            else if ((*filter).comparison == FilterInfo::Comparison::Equal)
                pass = (*args_.columns)[(*filter).filterColumn.colId][i] == (*filter).constant ? true : false;

            if (!pass) break;
        }

        /* Add it if pass == true */
        if (pass) args_.prefix++;
    }
}

int JobAllNonInterFilter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {

        /* Loop for all the predicates */
        bool pass;
        for (auto filter : (*args_.filterPtrs)) {
            pass = false;

            /* If it passes all the filter */
            if ((*filter).comparison == FilterInfo::Comparison::Less)
                pass = (*args_.columns)[(*filter).filterColumn.colId][i] < (*filter).constant ? true : false;
            else if ((*filter).comparison == FilterInfo::Comparison::Greater)
                pass = (*args_.columns)[(*filter).filterColumn.colId][i] > (*filter).constant? true : false;
            else if ((*filter).comparison == FilterInfo::Comparison::Equal)
                pass = (*args_.columns)[(*filter).filterColumn.colId][i] == (*filter).constant ? true : false;

            if (!pass) break;
        }

        /* Add it if pass == true */
        if (pass) args_.new_array[args_.prefix++] = i;
    }

    // free(&args_);
}


// Less filter Run functions
int JobLessNonInterFindSize::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[i] < args_.filter)
            args_.size++;
    }
}

int JobLessNonInterFilter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[i] < args_.filter)
            args_.new_array[args_.prefix++] = i;
    }

    // free(&args_);
}

// Greater filter Run functions
int JobGreaterNonInterFindSize::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[i] > args_.filter)
            args_.size++;
    }
}

int JobGreaterNonInterFilter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        //std::cerr << "Index is " << args_.prefix << '\n';
        if (args_.values[i] > args_.filter)
            args_.new_array[args_.prefix++] = i;
    }

    // free(&args_);
}

// Equal filter Run functions
int JobEqualNonInterFindSize::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[i] == args_.filter)
            args_.size++;
    }
}

int JobEqualNonInterFilter::Run() {
    for (size_t i = args_.low; i < args_.high; i++) {
        if (args_.values[i] == args_.filter)
            args_.new_array[args_.prefix++] = i;
    }

    // free(&args_);
}
