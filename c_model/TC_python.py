import networkx as nx
import numpy as np
import pandas as pd
from scipy import sparse
from scipy.sparse import csr_matrix
from scipy.sparse import coo_matrix
from time import perf_counter
import threading
from multiprocessing import Process, Pool
import math


partition_num = 3 ## can be set a variable, equals to thread numbers.
## dataset_name = 'as-skitter' ## .txt
## dataset_name = 'ca-cit-HepPh' ## .edges
## dataset_name = 'facebook_combined' ## .txt
## dataset_name = 'amazon0601' ## .mtx
## dataset_name = 'facebook_combined'
dataset_name = 'as20000102'
## dataset_name = 'test'
filename = '../datasets/' + dataset_name + '.txt'
print ("TC partition number = ", partition_num)

## multi-processors processing
def intersection_multi_process (processID, graph_array, csr_row, csr_col):
    t_inter_s = perf_counter()
    triangle_count = 0
    for i in range(len(graph_array)):
        first_list = csr_col[csr_row[int(graph_array[i][0])]: csr_row[int(graph_array[i][0])+1]]
        second_list = csr_col[csr_row[int(graph_array[i][1])]: csr_row[int(graph_array[i][1])+1]]
        common = set(first_list) & set(second_list)
        triangle_count += len(common)
    
    t_inter_e = perf_counter() - t_inter_s
    print("intersection finish, time : ", t_inter_e)
    return triangle_count

t_start = perf_counter()
print("Load data from hard-disk ... ")
txt_array_t = np.int64(np.loadtxt(filename))
txt_array = txt_array_t[:,:2]

print("Delete 'sumdegree = 1' edges ... ")
# Count the frequency of each element in the first column
outdegree = np.bincount(txt_array[:, 0])
indegree = np.bincount(txt_array[:, 1])
sumdegree = np.zeros(txt_array.shape[0])
padded_outdegree = np.pad(outdegree, (0, txt_array.shape[0] - len(outdegree)), mode='constant', constant_values=0)
padded_indegree = np.pad(indegree, (0, txt_array.shape[0] - len(indegree)), mode='constant', constant_values=0)
sumdegree = sumdegree + padded_indegree
sumdegree = sumdegree + padded_outdegree
# Find the indices of rows whose first element's frequency equals to 1
indices_to_delete = []
for i in range (txt_array.shape[0]) :
    if (sumdegree[txt_array[i][0]] == 1):
        indices_to_delete.append(i)
# Delete the rows whose first element's frequency equals to 1 from the original array
txt_array = np.delete(txt_array, indices_to_delete, axis=0)


## if source_id > dest_id, exchange them; only suitable for undirected graph.
for i in range (txt_array.shape[0]) :
    if (txt_array[i][0] >= txt_array[i][1]) :
        temp = txt_array[i][1]
        txt_array[i][1] = txt_array[i][0]
        txt_array[i][0] = temp

txt_array = txt_array[np.lexsort([txt_array.T[1]])] ## sort array by incremental order
txt_array = txt_array[np.lexsort([txt_array.T[0]])] ## sort array by incremental order

print("Delete self-edges ... ")
list_a = [] ## delete self-edges
for i in range (txt_array.shape[0]) :
    if (txt_array[i][0] == txt_array[i][1]) :
        list_a.append(i)
## print(list_a)
## print(txt_array)
txt_array = np.delete(txt_array, list_a, axis = 0)

print("Delete duplicated edges ... ")
list_b = []  ## delete duplicated edges 
for i in range (1, txt_array.shape[0]) :
    if ((txt_array[i][0] == txt_array[i-1][0]) & (txt_array[i][1] == txt_array[i-1][1])) :
        list_b.append(i)
## print(list_a)
txt_array = np.delete(txt_array, list_b, axis = 0)
## ======= load data done =======

## ======= convert to csr mode =======
global row_array, column_array
adj_matrix_dim = np.int64(txt_array.max()) + 1 ## get the max id for csr row size
print ("vertex range : 0 -", adj_matrix_dim, end = " ")
row_list = np.bincount(txt_array[:, 0], minlength=adj_matrix_dim)
row_array = np.add.accumulate(row_list)
row_array = np.insert(row_array, 0, 0) ## insert a 0 in the front.
column_array = txt_array[:, 1]   
print("indices:", row_array)
print("columns:", column_array)
print("indices:", row_array.size)
print("columns:", column_array.size)

## ======= partition function -- partition edges =======
global spilt_edge
spilt_edge = np.array_split(txt_array, partition_num, axis=0)
t_end = perf_counter()
print("our code preprocess elapses ", t_end - t_start)

np.savetxt("./dataset/" + dataset_name + "_row.txt", row_array, fmt='%d', delimiter=' ')
np.savetxt("./dataset/" + dataset_name + "_col.txt", column_array, fmt='%d', delimiter=' ')
for i in range(partition_num):
    np.savetxt("./dataset/" + dataset_name + "_edge_" + str(i) + ".txt", spilt_edge[i], fmt='%d', delimiter=' ')
    print ("save edge file ", i)

t_process_start = perf_counter()
intersection_pool = Pool(partition_num)
final_result_pool = []
for i in range(partition_num):
    final_result_pool.append(intersection_pool.apply_async(func=intersection_multi_process, args=(i, spilt_edge[i], row_array, column_array)))

intersection_pool.close()
intersection_pool.join()

result = 0
for i in range(partition_num):
    result += int(final_result_pool[i].get())

t_process_end = perf_counter()
print("our code execution elapses ", t_process_end - t_process_start)
print(result)



#### golden test
print("Golden test, using networkx")
print("Load graph data ... ")
testgraph_golden = nx.read_edgelist(filename, comments='#', delimiter=None, create_using=None, nodetype=None, data=True, edgetype=None, encoding='utf-8')
print("Load graph data done ")
t_golden_start = perf_counter()
triangle_golden = nx.triangles(testgraph_golden)
result_golden = int(sum(triangle_golden.values())/3)
t_golden_end = perf_counter()
print("networkx execution elapses ", t_golden_end - t_golden_start)
print (result_golden)
## print (triangle_golden)

