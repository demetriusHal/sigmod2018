#include <set>
#include <unordered_map>
#include <math.h>
#include "QueryPlan.hpp"

using namespace std;

void ColumnInfo::print() {
    cerr << "  min: "      << this->min << endl;
    cerr << "  max: "      << this->max << endl;
    cerr << "  size: "     << this->size << endl;
    cerr << "  distinct: " << this->distinct << endl;
    cerr << "  n: "        << this->n << endl;
    cerr << "  spread: "   << this->spread << endl << endl;
    flush(cerr);
}

// Estimates the new info of a node's column
// after a filter predicate is applied to that column
void JoinTreeNode::estimateInfoAfterFilter(FilterInfo& filterInfo) {
    if (filterInfo.comparison == FilterInfo::Comparison::Less) {
        this->columnInfo = estimateInfoAfterFilterLess(filterInfo.constant);
    }
    else if (filterInfo.comparison == FilterInfo::Comparison::Greater) {
        this->columnInfo = estimateInfoAfterFilterGreater(filterInfo.constant);
    }
    else if (filterInfo.comparison == FilterInfo::Comparison::Equal) {
        this->columnInfo = estimateInfoAfterFilterEqual(filterInfo.constant);
    }
}

// Returns the new column info
ColumnInfo JoinTreeNode::estimateInfoAfterFilterLess(const int constant) {
    ColumnInfo newColumnInfo;

    newColumnInfo.min      = this->columnInfo.min;
    newColumnInfo.max      = constant;
    newColumnInfo.distinct = (newColumnInfo.max - newColumnInfo.min) / this->columnInfo.spread;
    newColumnInfo.size     = newColumnInfo.distinct * (this->columnInfo.size / this->columnInfo.distinct);
    newColumnInfo.n        = newColumnInfo.max - newColumnInfo.min + 1;
    newColumnInfo.spread   = floor(newColumnInfo.n / newColumnInfo.distinct);

    return newColumnInfo;
}

// Returns the new column info
ColumnInfo JoinTreeNode::estimateInfoAfterFilterGreater(const int constant) {
    ColumnInfo newColumnInfo;

    newColumnInfo.min      = constant;
    newColumnInfo.max      = this->columnInfo.max;
    newColumnInfo.distinct = (newColumnInfo.max - newColumnInfo.min) / this->columnInfo.spread;
    newColumnInfo.size     = newColumnInfo.distinct * (this->columnInfo.size / this->columnInfo.distinct);
    newColumnInfo.n        = newColumnInfo.max - newColumnInfo.min + 1;
    newColumnInfo.spread   = floor(newColumnInfo.n / newColumnInfo.distinct);

    return newColumnInfo;
}

// Returns the new column info
ColumnInfo JoinTreeNode::estimateInfoAfterFilterEqual(const int constant) {
    ColumnInfo newColumnInfo;

    newColumnInfo.min      = constant;
    newColumnInfo.max      = constant;
    newColumnInfo.distinct = 1;
    newColumnInfo.size     = this->columnInfo.size / this->columnInfo.distinct;
    newColumnInfo.n        = 1;
    newColumnInfo.spread   = 1;

    return newColumnInfo;
}

// Returns the new column info
ColumnInfo JoinTreeNode::estimateInfoAfterJoin(ColumnInfo& leftColumnInfo, ColumnInfo& rightColumnInfo, const int tuples) {
    ColumnInfo newColumnInfo;

    newColumnInfo.min      = min(leftColumnInfo.min, rightColumnInfo.min);
    newColumnInfo.max      = max(leftColumnInfo.max, rightColumnInfo.max);
    newColumnInfo.n        = newColumnInfo.max - newColumnInfo.min + 1;
    newColumnInfo.distinct = (leftColumnInfo.distinct * rightColumnInfo.distinct) / newColumnInfo.n;
    newColumnInfo.size     = tuples;
    newColumnInfo.spread   = floor((newColumnInfo.max - newColumnInfo.min + 1) / (newColumnInfo.distinct));

    return newColumnInfo;
}

// Construct a plan tree from set of relations IDs
JoinTree* JoinTree::build(QueryInfo& queryInfo, ColumnInfo** columnInfos) {
    // Maps every possible set of relations to its respective best plan tree
    unordered_map< vector<bool>, JoinTree* > BestTree;
    int relationsCount = queryInfo.relationIds.size();

    // Initialise the BestTree structure with nodes
    // for every single relation in the input
    for (int i = 0; i < relationsCount; i++) {
        // Allocate memory
        JoinTree* joinTreePtr = (JoinTree*) malloc(sizeof(JoinTree));
        JoinTreeNode* joinTreeNodePtr = (JoinTreeNode*) malloc(sizeof(JoinTreeNode));

        // Initialise JoinTreeNode
        joinTreeNodePtr->nodeId = i; // The binding of the relation
        joinTreeNodePtr->left = NULL;
        joinTreeNodePtr->right = NULL;
        joinTreeNodePtr->parent = NULL;
        //joinTreeNodePtr->predicatePtr = NULL;
        joinTreeNodePtr->filterPtr = NULL;

        // Initialise JoinTree
        joinTreePtr->root = joinTreeNodePtr;

        // Insert into the BestTree
        vector<bool> relationToVector(relationsCount, false);
        relationToVector[i] = true;
        BestTree[relationToVector] = joinTreePtr;
    }

    // Maps all sets of a certain size to their size
    // Sets are represented as vector<bool>
    map< int, set< vector<bool> > > powerSetMap;

    // Generate power-set of the given set of relations
    // source: www.geeksforgeeks.org/power-set/
    unsigned int powerSetSize = pow(2, relationsCount);

    for (int counter = 0; counter < powerSetSize; counter++) {
        vector<bool> tempVec(relationsCount, false);
        int setSize = 0;

        for (int j = 0; j < relationsCount; j++) {
            if (counter & (1 << j)) {
                tempVec[j] = true;
                setSize++;
            }

            // Save all sets of a certain size
            powerSetMap[setSize].insert(tempVec);
        }
    }

    // Apply all the filters first
    for (int i=0; i < queryInfo.filters.size(); i++) {
        // Update the tree (containing a single node)
        // of the relation whose column will be filtered
        vector<bool> relationToVector(relationsCount, false);
        relationToVector[queryInfo.filters[i].filterColumn.binding] = true;

        const int relationId = queryInfo.filters[i].filterColumn.relId;
        const int columnId = queryInfo.filters[i].filterColumn.colId;
        BestTree[relationToVector]->root->filterPtr = &(queryInfo.filters[i]);
        BestTree[relationToVector]->root->columnInfo = columnInfos[relationId][columnId];

        // Update the column info
        BestTree[relationToVector]->root->estimateInfoAfterFilter(queryInfo.filters[i]);
    }

    // Dynamic programming algorithm
    for (int i = 1; i <= relationsCount; i++) {
        for (auto s : powerSetMap[i]) {
            for (int j = 0; j < relationsCount; j++) {
                // If j is not in the set
                if (s[j] == false) {
                    // Check if there is a corresponding predicate
                    bool predicateFound = false;
                    for (auto predicate : queryInfo.predicates) {
                        // If the right relation is found on the left hand side of a predicate
                        /*
                        if (predicate.left.binding == j) {
                            for (int n = 0; n < relationsCount; n++) {
                                if ((s[n] == true) && (predicate.right.binding == n)) {
                                    predicateFound = true;
                                    break;
                                }
                            }
                        }
                        */
                        // If the right relation is found on the left hand side of a predicate
                        if (predicate.right.binding == j) {
                            for (int n = 0; n < relationsCount; n++) {
                                if ((s[n] == true) && (predicate.left.binding == n)) {
                                    predicateFound = true;
                                    break;
                                }
                            }
                        }

                        if (predicateFound == true) break;
                    }

                    if (predicateFound == true) {
                        // Create the bit vector representation of the relation j
                        vector<bool> relationToVector(relationsCount, false);
                        relationToVector[j] = true;

                        // Merge the two trees
                        JoinTree* currTree = CreateJoinTree(BestTree[s], BestTree[relationToVector]);

                        // Save the new merged tree
                        vector<bool> s1 = s;
                        s1[j] = true;

                        // cerr << "comparing costs\n";
                        if (BestTree[s1] == NULL || cost(BestTree[s1]) > cost(currTree)) {
                            BestTree[s1] = currTree;
                        }
                    }
                }
            }
        }
    }

    // Create a set of the predicates
    set<PredicateInfo*> predicatesSet;
    for (int i = 0; i < queryInfo.predicates.size(); i++) {
        predicatesSet.insert(&(queryInfo.predicates[i]));
    }

    // Set the predicates for every node
    vector<bool> rootToVector(relationsCount, true);
    JoinTreeNode* joinTreeNodePtr = BestTree[rootToVector]->root;
    set<int> joinedNodes; // Keep the bindings of the already joined nodes

    // Go to the leftmost join
    while (joinTreeNodePtr->left->nodeId == -1) {
        joinTreeNodePtr = joinTreeNodePtr->left;
    }

    // Go bottom-up and save the corresponding predicates
    joinedNodes.insert(joinTreeNodePtr->left->nodeId);

    while (1) {
        bool predicateFound = false;
        for (auto predicate : predicatesSet) {
            // If the right relation is found on the left hand side of a predicate
            /*
            if (predicate->left.binding == joinTreeNodePtr->right->nodeId) {
                for (auto n : joinedNodes) {
                    if (predicate->right.binding == n) {
                        joinTreeNodePtr->predicatePtr = predicate;
                        predicatesSet.erase(predicate);
                        joinedNodes.insert(joinTreeNodePtr->right->nodeId);
                        predicateFound = true;
                        break;
                    }
                }
            }*/
            // If the right relation is found on the left hand side of a predicate
            if (predicate->right.binding == joinTreeNodePtr->right->nodeId) {
                for (auto n : joinedNodes) {
                    if (predicate->left.binding == n) {
                        joinTreeNodePtr->predicatePtr = predicate;
                        predicatesSet.erase(predicate);
                        joinedNodes.insert(joinTreeNodePtr->right->nodeId);
                        predicateFound = true;
                        break;
                    }
                }
            }

            if (predicateFound == true) break;
        }

        // Go to parent
        if (joinTreeNodePtr->parent != NULL) {
            joinTreeNodePtr = joinTreeNodePtr->parent;
        }
        else {
            break;
        }
    }

    // Return the optimal tree
    if (predicatesSet.size() != 0) {
        // Merge the self joins with the root
        return AddFilterJoin(BestTree[rootToVector], *(predicatesSet.begin()));
    }
    else {
        return BestTree[rootToVector];
    }
}

// Merges two join trees
JoinTree* JoinTree::CreateJoinTree(JoinTree* leftTree, JoinTree* rightTree) {
    if ((leftTree == NULL) || (rightTree == NULL)) {
        return NULL;
    }

    // Allocate memory for the new tree
    JoinTree* joinTreePtr = (JoinTree*) malloc(sizeof(JoinTree));
    JoinTreeNode* joinTreeNodePtr = (JoinTreeNode*) malloc(sizeof(JoinTreeNode));

    // Initialise the new JoinTreeNode
    joinTreeNodePtr->nodeId = -1; // This is an intermediate node
    joinTreeNodePtr->left = leftTree->root;
    joinTreeNodePtr->right = rightTree->root;
    joinTreeNodePtr->parent = NULL;
    joinTreeNodePtr->predicatePtr = NULL;
    joinTreeNodePtr->filterPtr = NULL;

    // Initialise the new JoinTree
    joinTreePtr->root = joinTreeNodePtr;

    // Update the parent pointers of the merged trees
    leftTree->root->parent = joinTreePtr->root;
    rightTree->root->parent = joinTreePtr->root;

    // Estimate the new info of the merged columns

    return joinTreePtr;
}

// Merges the final optimal tree with a filter join predicate
JoinTree* JoinTree::AddFilterJoin(JoinTree* leftTree, PredicateInfo* predicateInfo) {
    // Allocate memory for the new tree
    JoinTree* joinTreePtr = (JoinTree*) malloc(sizeof(JoinTree));
    JoinTreeNode* joinTreeNodePtr = (JoinTreeNode*) malloc(sizeof(JoinTreeNode));

    // Initialise the new JoinTreeNode
    joinTreeNodePtr->nodeId = -1; // This is an intermediate node
    joinTreeNodePtr->left = leftTree->root;
    joinTreeNodePtr->right = NULL;
    joinTreeNodePtr->parent = NULL;
    joinTreeNodePtr->predicatePtr = predicateInfo;
    joinTreeNodePtr->filterPtr = NULL;

    // Initialise the new JoinTree
    joinTreePtr->root = joinTreeNodePtr;

    // Update the parent pointers of the merged trees
    leftTree->root->parent = joinTreePtr->root;
    return joinTreePtr;
}

// execute the plan described by a Plan Tree
table_t* JoinTreeNode::execute(JoinTreeNode* joinTreeNodePtr, Joiner& joiner, QueryInfo& queryInfo) {
    JoinTreeNode *left  = joinTreeNodePtr->left;
    JoinTreeNode *right = joinTreeNodePtr->right;
    table_t *table_l;
    table_t *table_r;
    table_t *res;

    // Leaf node containing a single relation
    if (left == NULL && right == NULL) {
        res = joiner.CreateTableTFromId(queryInfo.relationIds[joinTreeNodePtr->nodeId], joinTreeNodePtr->nodeId);

        // Apply the filter
        if (joinTreeNodePtr->filterPtr != NULL) {
            joiner.AddColumnToTableT(joinTreeNodePtr->filterPtr->filterColumn, res);
            joiner.Select(*(joinTreeNodePtr->filterPtr), res);
        }

        return res;
    }

    // Go left
    table_l = joinTreeNodePtr->execute(left, joiner, queryInfo);

    // This is an intermediate node (join)
    if (right != NULL) {
        table_r = joinTreeNodePtr->execute(right, joiner, queryInfo);
        joiner.AddColumnToTableT(joinTreeNodePtr->predicatePtr->left, table_l);
        joiner.AddColumnToTableT(joinTreeNodePtr->predicatePtr->right, table_r);
        return joiner.join(table_l, table_r, *joinTreeNodePtr->predicatePtr);
    }
    else {
        return joiner.SelfJoin(table_l, joinTreeNodePtr->predicatePtr);
    }
}

// Estimates the cost of a given Plan Tree
double JoinTreeNode::cost() {
    double nodeCostEstimation = 1.0;

    // if it is a leaf or a filter
    if ((this->filterPtr == NULL && this->predicatePtr == NULL) || this->filterPtr != NULL)
        nodeCostEstimation = 0;
    // if it is a join
    else if (this->predicatePtr != NULL && this->left != NULL && this->right != NULL) {
        int i = this->left->columnInfo.size / 10000, j = this->right->columnInfo.size / 10000;
        nodeCostEstimation = smallDiffRelJoin[i][j];
        // // if it is a self join
        // if (this->left->nodeId != -1 && this->left->nodeId == this->right->nodeId)
        //     nodeCostEstimation += (this->left->columnInfo.size * this->left->columnInfo.size) / this->left->columnInfo.distinct;
        // // if left relation may be a subset of the right
        // else if ((this->left->columnInfo.min >= this->right->columnInfo.min) &&
        // (this->left->columnInfo.max <= this->right->columnInfo.max))
        //     nodeCostEstimation += (this->left->columnInfo.size * this->right->columnInfo.size) / this->right->columnInfo.distinct;
        // // if right relation may be a subset of the right
        // else if ((this->left->columnInfo.min <= this->right->columnInfo.min) &&
        // (this->left->columnInfo.max >= this->right->columnInfo.max))
        //     nodeCostEstimation += (this->left->columnInfo.size * this->right->columnInfo.size) / this->left->columnInfo.distinct;
        // // if the columns may be independent
        // else
        //     nodeCostEstimation += (this->left->columnInfo.size * this->right->columnInfo.size) / this->left->columnInfo.n;
    }

    if (this->left != NULL && this->right != NULL)
        nodeCostEstimation += this->left->cost() + this->right->cost();
    else if (this->left != NULL)
        nodeCostEstimation += this->left->cost();
    else if (this->right != NULL)
        nodeCostEstimation += this->right->cost();

    return nodeCostEstimation;
}

// Estimates the cost of a given Plan Tree
double JoinTree::cost(JoinTree* joinTreePtr) {
    if (joinTreePtr != NULL && joinTreePtr->root != NULL)
       return joinTreePtr->root->cost();
    // return 1.0;
}

void JoinTreeNode::print(JoinTreeNode* joinTreeNodePtr) {
    if (joinTreeNodePtr == NULL) {
        return;
    }

    int depth = 0;

    while (joinTreeNodePtr->nodeId == -1) {
        for (int i=0; i < depth; i++) fprintf(stderr,"    ");
        fprintf(stderr, "In node with predicate: ");
        fprintf(stderr,"%d.%d=%d.%d\n", joinTreeNodePtr->predicatePtr->left.binding,
            joinTreeNodePtr->predicatePtr->left.colId, joinTreeNodePtr->predicatePtr->right.binding,
            joinTreeNodePtr->predicatePtr->right.colId);

        if (joinTreeNodePtr->right != NULL) {
            for (int i=0; i < depth; i++) fprintf(stderr,"    ");
            fprintf(stderr, "Right child has id = %d\n", joinTreeNodePtr->right->nodeId);
            if (joinTreeNodePtr->right->filterPtr != NULL) {
                for (int i=0; i < depth; i++) fprintf(stderr,"    ");
                fprintf(stderr, "Right child has filter = ");
                fprintf(stderr,"%d.%d %c %ld\n", joinTreeNodePtr->right->filterPtr->filterColumn.binding,
                    joinTreeNodePtr->right->filterPtr->filterColumn.colId,
                    joinTreeNodePtr->right->filterPtr->comparison, joinTreeNodePtr->right->filterPtr->constant);
            }
            else {
                for (int i=0; i < depth; i++) fprintf(stderr,"    ");
                fprintf(stderr, "Right child has no filter\n");
            }
        }
        else {
            for (int i=0; i < depth; i++) fprintf(stderr,"    ");
            fprintf(stderr, "Node has no right child so this is a filter join\n");
        }

        for (int i=0; i < depth; i++) fprintf(stderr,"    ");
        fprintf(stderr, "Left child has id = %d\n", joinTreeNodePtr->left->nodeId);
        if (joinTreeNodePtr->left->filterPtr != NULL) {
            for (int i=0; i < depth; i++) fprintf(stderr,"    ");
            fprintf(stderr, "Left child has filter = ");
            fprintf(stderr,"%d.%d %c %ld\n", joinTreeNodePtr->left->filterPtr->filterColumn.binding,
                joinTreeNodePtr->left->filterPtr->filterColumn.colId,
                joinTreeNodePtr->left->filterPtr->comparison, joinTreeNodePtr->left->filterPtr->constant);
        }
        else {
            for (int i=0; i < depth; i++) fprintf(stderr,"    ");
            fprintf(stderr, "Left child has no filter\n");
        }

        joinTreeNodePtr = joinTreeNodePtr->left;
        depth++;
    }
    fprintf(stderr, "\n");
}

// Fills the columnInfo matrix with the data of every column
void QueryPlan::fillColumnInfo(Joiner& joiner) {
    Relation* relation;
    int relationsCount = joiner.getRelationsCount(); // Get the number of relations
    int columnsCount; // Number of columns of a relation

    // Allocate memory for every relation
    columnInfos = (ColumnInfo**) malloc(relationsCount * sizeof(ColumnInfo*));

    // For every relation get its column statistics
    for (int rel = 0; rel < relationsCount; rel++) {
        // Get the number of columns
        relation = &(joiner.getRelation(rel));
        columnsCount = relation->columns.size();

        // Allocate memory for the columns
        columnInfos[rel] = (ColumnInfo*) malloc(columnsCount * sizeof(ColumnInfo));

        // Get the info of every column
        for (int col = 0; col < columnsCount; col++) {
            uint64_t minimum = numeric_limits<uint64_t>::max(); // Value of minimum element
            uint64_t maximum = 0; // Value of maximum element
            set<uint64_t> distinctElements; // Keep the distinct elements of the column

            uint64_t tuples = relation->size;
            for (int i = 0; i < tuples; i++) {
                uint64_t element = relation->columns[col][i];
                if (element > maximum) maximum = element;
                if (element < minimum) minimum = element;
                distinctElements.insert(element);
            }

            // Save the infos
            columnInfos[rel][col].min = minimum;
            columnInfos[rel][col].max = maximum;
            columnInfos[rel][col].size = tuples;
            columnInfos[rel][col].distinct = (uint64_t) distinctElements.size();
            columnInfos[rel][col].n = maximum - minimum + 1;
            columnInfos[rel][col].spread = floor((maximum - minimum + 1) / (columnInfos[rel][col].distinct));
        }
    }
}

// JoinTree destructor
void JoinTree::destrJoinTree() {
    /* destruct query-tree in a DFS fassion */
    JoinTreeNode *currNodePtr = this->root;
    bool from_left = false;

    while(currNodePtr) {
        /* if there are left children */
        if (currNodePtr->left) {
            currNodePtr = currNodePtr->left;
            from_left = true;
        }
        /* if there are right children */
        else if (currNodePtr->right) {
            currNodePtr = currNodePtr->right;
            from_left = false;
        }
        /* if there are no left or right children */
        else {
            /* clean-up node */
            free(currNodePtr->filterPtr);
            currNodePtr->filterPtr = NULL;
            free(currNodePtr->predicatePtr);
            currNodePtr->predicatePtr = NULL;
            /* essentially, not head node */
            if (currNodePtr->parent) {
                /* go to the parent */
                currNodePtr = currNodePtr->parent;
                /* free the correct child's node */
                if (from_left) {
                    free(currNodePtr->left);
                    currNodePtr->left = NULL;
                }
                else {
                    free(currNodePtr->right);
                    currNodePtr->right = NULL;
                }
            }
            /* essentially, head node */
            else {
                free(currNodePtr);
                currNodePtr = NULL;
            }
        }
    }
}

// QueryPlan destructor
void QueryPlan::destrQueryPlan(Joiner& joiner) {
    int relationsCount = joiner.getRelationsCount(); // Get the number of relations

    joinTreePtr->destrJoinTree();

    // For every relation get its column statistics
    for (int rel = 0; rel < relationsCount; rel++)
        // Allocate memory for the columns
        free(columnInfos[rel]);

    free(columnInfos);
}
