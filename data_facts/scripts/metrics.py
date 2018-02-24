#!/usr/bin/env python

# CREATED BY ORESTIS
# ADDITIONS BY YORGOS

import argparse
import sys
import collections
import operator
import matplotlib.pyplot as plt

# for specific joins dictionary filtering
LOWER_BOUND = 15
UPPER_BOUND = 25
WINDOW = 2

# parse command line arguments
parser = argparse.ArgumentParser(description='Give me a .work path')
parser.add_argument('path', metavar='P', type=str, nargs='+',
                   help='a path for the accumulator')

args = parser.parse_args()

if len(sys.argv) > 2:
    sys.exit(1)

path = sys.argv[1]

# Dictionary that maps number of joins
# to their frequency in the queries
total_join_dict = {}
# Dictionary that maps number of filters
# to their frequency in the queries
total_filter_dict = {}
# Dictionary that maps number of tables
# to their frequency in the queries
total_tables_dict = {}
# Dictionary that maps number of columns
# to their respective tables
columns_dict = {}
# Dictionary of dictionaries that maps
# the number of columns of each table
# to their frequency in that table
columns_in_table_dict = {}
# Dictionary that maps the specific joins
# to their frequency in the queries
specific_joins_dict = {}

queries_counter = 0

# Open the file that contains the queries
with open(path, "r") as file:
    for line in file:
        # Remove the trailing newline character
        line = line.split("\n")[0];

        # Skip lines with an "F"
        if line == "F":
            continue

        # Set that collects the different tables in each query
        tables = set()

        # Extract the predicates
        line = line.split("|")[1]
        predicates = line.split("&")

        # update counters
        join_counter = 0
        filter_counter = 0
        queries_counter += 1

        # Count the join predicates
        for predicate in predicates:
            # Check for equality and inequalities
            # if it is an equality it can either be a join or a filter predicate
            if "=" in predicate:
                predicate = predicate.split("=")

                # Check if both sides of the predicate
                # contain a dot (.) which means
                # it is a join predicate
                # otherwise, it is a filter predicate
                if (("." in predicate[0]) and ("." in predicate[1])):
                    join_counter += 1
                    # Update the dictionary
                    if join_counter in total_join_dict:
                        total_join_dict[join_counter] += 1
                    else:
                        total_join_dict[join_counter] = 1

                    # Extract the tables
                    left_predicate = predicate[0].split(".")
                    right_predicate = predicate[1].split(".")
                    tables.add(left_predicate[0])
                    tables.add(right_predicate[0])

                    if left_predicate[0] in columns_dict:
                        columns_dict[left_predicate[0]] += 1
                    else:
                        columns_dict[left_predicate[0]] = 1

                    if right_predicate[0] in columns_dict:
                        columns_dict[right_predicate[0]] += 1
                    else:
                        columns_dict[right_predicate[0]] = 1

                    if left_predicate[0] in columns_in_table_dict:
                        if left_predicate[1] in columns_in_table_dict[left_predicate[0]]:
                            columns_in_table_dict[left_predicate[0]][left_predicate[1]] += 1
                        else:
                            columns_in_table_dict[left_predicate[0]][left_predicate[1]] = 1
                    else:
                        columns_in_table_dict[left_predicate[0]] = {}
                        columns_in_table_dict[left_predicate[0]][left_predicate[1]] = 1

                    if right_predicate[0] in columns_in_table_dict:
                        if right_predicate[1] in columns_in_table_dict[right_predicate[0]]:
                            columns_in_table_dict[right_predicate[0]][right_predicate[1]] += 1
                        else:
                            columns_in_table_dict[right_predicate[0]][right_predicate[1]] = 1
                    else:
                        columns_in_table_dict[right_predicate[0]] = {}
                        columns_in_table_dict[right_predicate[0]][right_predicate[1]] = 1

                    join_str = predicate[0] + " = " + predicate[1]
                    rev_join_str = predicate[1] + " = " + predicate[0]

                    if join_str in specific_joins_dict:
                        specific_joins_dict[join_str] += 1
                    elif rev_join_str in specific_joins_dict:
                        specific_joins_dict[rev_join_str] += 1
                    else:
                        specific_joins_dict[join_str] = 1

                # otherwise, it is a filter predicate
                elif (("." in predicate[0]) and not ("." in predicate[1])) or (not ("." in predicate[0]) and ("." in predicate[1])):
                    filter_counter += 1

                    # Update the dictionary
                    if filter_counter in total_filter_dict:
                        total_filter_dict[filter_counter] += 1
                    else:
                        total_filter_dict[filter_counter] = 1

                    if "." in predicate[0]:
                        left_predicate = predicate[0].split(".")
                        if left_predicate[0] in columns_dict:
                            columns_dict[left_predicate[0]] += 1
                        else:
                            columns_dict[left_predicate[0]] = 1

                        if left_predicate[0] in columns_in_table_dict:
                            if left_predicate[1] in columns_in_table_dict[left_predicate[0]]:
                                columns_in_table_dict[left_predicate[0]][left_predicate[1]] += 1
                            else:
                                columns_in_table_dict[left_predicate[0]][left_predicate[1]] = 1
                        else:
                            columns_in_table_dict[left_predicate[0]] = {}
                            columns_in_table_dict[left_predicate[0]][left_predicate[1]] = 1
                    else:
                        right_predicate = predicate[1].split(".")
                        if right_predicate[0] in columns_dict:
                            columns_dict[right_predicate[0]] += 1
                        else:
                            columns_dict[right_predicate[0]] = 1

                        if right_predicate[0] in columns_in_table_dict:
                            if right_predicate[1] in columns_in_table_dict[right_predicate[0]]:
                                columns_in_table_dict[right_predicate[0]][right_predicate[1]] += 1
                            else:
                                columns_in_table_dict[right_predicate[0]][right_predicate[1]] = 1
                        else:
                            columns_in_table_dict[right_predicate[0]] = {}
                            columns_in_table_dict[right_predicate[0]][right_predicate[1]] = 1

            # it must be a filter predicate
            elif ">" in predicate:
                predicate = predicate.split(">")

                # Confirm that not both sides of the predicate
                # contain a dot (.) which means
                # it is a filter predicate indeed
                if (("." in predicate[0]) and not ("." in predicate[1])) or (not ("." in predicate[0]) and ("." in predicate[1])):
                    filter_counter += 1
                    # Update the dictionary
                    if filter_counter in total_filter_dict:
                        total_filter_dict[filter_counter] += 1
                    else:
                        total_filter_dict[filter_counter] = 1

                    if "." in predicate[0]:
                        left_predicate = predicate[0].split(".")
                        if left_predicate[0] in columns_dict:
                            columns_dict[left_predicate[0]] += 1
                        else:
                            columns_dict[left_predicate[0]] = 1

                        if left_predicate[0] in columns_in_table_dict:
                            if left_predicate[1] in columns_in_table_dict[left_predicate[0]]:
                                columns_in_table_dict[left_predicate[0]][left_predicate[1]] += 1
                            else:
                                columns_in_table_dict[left_predicate[0]][left_predicate[1]] = 1
                        else:
                            columns_in_table_dict[left_predicate[0]] = {}
                            columns_in_table_dict[left_predicate[0]][left_predicate[1]] = 1
                    else:
                        right_predicate = predicate[1].split(".")
                        if right_predicate[0] in columns_dict:
                            columns_dict[right_predicate[0]] += 1
                        else:
                            columns_dict[right_predicate[0]] = 1

                        if right_predicate[0] in columns_in_table_dict:
                            if right_predicate[1] in columns_in_table_dict[right_predicate[0]]:
                                columns_in_table_dict[right_predicate[0]][right_predicate[1]] += 1
                            else:
                                columns_in_table_dict[right_predicate[0]][right_predicate[1]] = 1
                        else:
                            columns_in_table_dict[right_predicate[0]] = {}
                            columns_in_table_dict[right_predicate[0]][right_predicate[1]] = 1

            # it must be a filter predicate
            elif "<" in predicate:
                predicate = predicate.split("<")

                # Confirm that not both sides of the predicate
                # contain a dot (.) which means
                # it is a filter predicate indeed
                if (("." in predicate[0]) and not ("." in predicate[1])) or (not ("." in predicate[0]) and ("." in predicate[1])):
                    filter_counter += 1
                    # Update the dictionary
                    if filter_counter in total_filter_dict:
                        total_filter_dict[filter_counter] += 1
                    else:
                        total_filter_dict[filter_counter] = 1

                    if "." in predicate[0]:
                        left_predicate = predicate[0].split(".")
                        if left_predicate[0] in columns_dict:
                            columns_dict[left_predicate[0]] += 1
                        else:
                            columns_dict[left_predicate[0]] = 1

                        if left_predicate[0] in columns_in_table_dict:
                            if left_predicate[1] in columns_in_table_dict[left_predicate[0]]:
                                columns_in_table_dict[left_predicate[0]][left_predicate[1]] += 1
                            else:
                                columns_in_table_dict[left_predicate[0]][left_predicate[1]] = 1
                        else:
                            columns_in_table_dict[left_predicate[0]] = {}
                            columns_in_table_dict[left_predicate[0]][left_predicate[1]] = 1
                    else:
                        right_predicate = predicate[1].split(".")
                        if right_predicate[0] in columns_dict:
                            columns_dict[right_predicate[0]] += 1
                        else:
                            columns_dict[right_predicate[0]] = 1

                        if right_predicate[0] in columns_in_table_dict:
                            if right_predicate[1] in columns_in_table_dict[right_predicate[0]]:
                                columns_in_table_dict[right_predicate[0]][right_predicate[1]] += 1
                            else:
                                columns_in_table_dict[right_predicate[0]][right_predicate[1]] = 1
                        else:
                            columns_in_table_dict[right_predicate[0]] = {}
                            columns_in_table_dict[right_predicate[0]][right_predicate[1]] = 1
            # END if

        # Update the dictionary
        if queries_counter in total_tables_dict:
            total_tables_dict[len(tables)] += 1
        else:
            total_tables_dict[len(tables)] = 1
        #END for
    #END for
# END with

# close .work file
file.close()

# Sort dictionaries
sorted_join_dict = collections.OrderedDict(sorted(total_join_dict.items()))
sorted_filter_dict = collections.OrderedDict(sorted(total_filter_dict.items()))
sorted_tables_dict = collections.OrderedDict(sorted(total_tables_dict.items()))
sorted_columns_dict = collections.OrderedDict(sorted(columns_dict.items()))
sorted_columns_in_table_dict = collections.OrderedDict(sorted(columns_in_table_dict.items(), key=operator.itemgetter(0))) # doesn't work as desired
sorted_specific_joins_dict = collections.OrderedDict(sorted(specific_joins_dict.items(), key=operator.itemgetter(1), reverse=True))

# Plot the join data
print("Frequency of joins per query")
plt.bar(range(len(sorted_join_dict)), list(sorted_join_dict.values()), align='center')
plt.xticks(range(len(sorted_join_dict)), list(sorted_join_dict.keys()))
plt.title("Frequency of joins per query")
plt.xlabel("Number of joins")
plt.ylabel("Frequency of joins")
plt.show()

# Plot the filter data
print("Frequency of filters per query")
plt.bar(range(len(sorted_filter_dict)), list(sorted_filter_dict.values()), align='center')
plt.xticks(range(len(sorted_filter_dict)), list(sorted_filter_dict.keys()))
plt.title("Frequency of filters per query")
plt.xlabel("Number of filters")
plt.ylabel("Frequency of filters")
plt.show()

# Plot the tables data
print("Frequency of tables per query")
plt.bar(range(len(sorted_tables_dict)), list(sorted_tables_dict.values()), align='center')
plt.xticks(range(len(sorted_tables_dict)), list(sorted_tables_dict.keys()))
plt.title("Frequency of tables per query")
plt.xlabel("Number of tables")
plt.ylabel("Frequency of tables")
plt.show()

# Plot the columns data
print("Columns per table")
plt.bar(range(len(sorted_columns_dict)), list(sorted_columns_dict.values()), align='center')
plt.xticks(range(len(sorted_columns_dict)), list(sorted_columns_dict.keys()))
plt.title("Columns per table")
plt.xlabel("Number of table")
plt.ylabel("Number of columns")
plt.show()

# Plot the columns in table data
print("Columns from each table")
for table in columns_in_table_dict:
    sorted_columns_in_table_table_dict = collections.OrderedDict(sorted(sorted_columns_in_table_dict[table].items()))

    # Plot the columns frequency data
    print("Columns in table " + table)
    plt.bar(range(len(sorted_columns_in_table_table_dict)), list(sorted_columns_in_table_table_dict.values()), align='center')
    plt.xticks(range(len(sorted_columns_in_table_table_dict)), list(sorted_columns_in_table_table_dict.keys()))
    plt.title("Columns in table " + table + " frequency")
    plt.xlabel("Number of columns")
    plt.ylabel("Frequency of columns")
    plt.show()

# Plot the specific joins data
print("Frequency of specific joins per query")
plt.bar(range(len(sorted_specific_joins_dict)), list(sorted_specific_joins_dict.values()), align='center')
plt.xticks(range(len(sorted_specific_joins_dict)), list(sorted_specific_joins_dict.keys()))
plt.title("Frequency of specific joins per query")
plt.xlabel("Joins")
plt.ylabel("Frequency of joins")
plt.show()

# Plot the specific joins data filtered
sorted_specific_joins_dict_filtered_middle = dict((k, v) for k, v in sorted_specific_joins_dict.items() if (v >= LOWER_BOUND and v <= UPPER_BOUND))
sorted_specific_joins_dict_filtered_middle = collections.OrderedDict(sorted(sorted_specific_joins_dict_filtered_middle.items(), key=operator.itemgetter(1), reverse=True))
sorted_specific_joins_dict_filtered = {}
elements = 0
for key in sorted_specific_joins_dict_filtered_middle:
    if elements < WINDOW:
        sorted_specific_joins_dict_filtered[key] = sorted_specific_joins_dict_filtered_middle[key]
    elements += 1
sorted_specific_joins_dict_filtered = collections.OrderedDict(sorted(sorted_specific_joins_dict_filtered.items(), key=operator.itemgetter(1), reverse=True))

print("Frequency of specific joins per query. LB = " + str(LOWER_BOUND) + ", UB = " + str(UPPER_BOUND) + ", W = " + str(WINDOW))
plt.bar(range(len(sorted_specific_joins_dict_filtered)), list(sorted_specific_joins_dict_filtered.values()), align='center')
plt.xticks(range(len(sorted_specific_joins_dict_filtered)), list(sorted_specific_joins_dict_filtered.keys()))
plt.title("Frequency of specific joins per query filtered")
plt.xlabel("Joins filtered")
plt.ylabel("Frequency of joins filtered")
plt.show()
