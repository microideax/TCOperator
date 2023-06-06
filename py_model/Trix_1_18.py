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


## use multi-thread method to implement graph partition, function defination
def partion_multi_process (processID, graph_array, index_list, length):
    t_m_p = perf_counter()
    csr_row = np.int32(np.zeros(length + 1))
    csr_col = []
    temp_list = []
    for i in range(len(graph_array)):
        if ((int(graph_array[i][1]) >= index_list[processID]) and (int(graph_array[i][1]) < index_list[processID+1])):
            ## csr
            temp_list.append([graph_array[i][0], graph_array[i][1]])
            csr_row[graph_array[i][0]+1] += 1
    if (temp_list == []):
        return [], []

    temp_list = np.array(temp_list)
    temp_list = temp_list[np.lexsort([temp_list.T[1]])] ## sort array by incremental order
    temp_list = temp_list[np.lexsort([temp_list.T[0]])] ## sort array by incremental order

    csr_row = np.add.accumulate(csr_row)
    csr_col = temp_list[:, 1]

    t_m_e = perf_counter() - t_m_p
    print("partition finish, time : ", t_m_e)
    
    return csr_row, csr_col


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


## filename = './datasets/as-skitter.txt'
## filename = './datasets/amazon0601.mtx' ## seem our method performs better in amamzon dataset
## filename = './datasets/roadNet-CA.txt'
## filename = './datasets/ca-cit-HepPh.edges'
filename = './datasets/cit-Patents.txt'

print("Load data from hard-disk ... ")
txt_array_t = np.int64(np.loadtxt(filename))
txt_array = txt_array_t[:,:2]

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
dest_array = txt_array[:, 1]
dest_array = np.bincount(dest_array)
dest_array = np.add.accumulate(dest_array)
## print (dest_array.shape[0])
print (dest_array)

global _shared_array
_shared_array = txt_array

print("Load data done ")
## ======= load data done =======


## define the partition index, need to add partition function here
t_partition_start = perf_counter()
adj_matrix_dim = np.int64(txt_array.max()) + 1 ## get the max id for csr row size
print ("vertex range : 0 -", adj_matrix_dim, end = " ")
partition_num = 8 ## can be set a variable, equals to thread numbers.
print ("using process number :", partition_num)
partition_index = np.zeros(partition_num + 1, dtype=np.int32)
for i in range(partition_num - 1):
    index_t = np.int32(len(txt_array)*(i+1)/partition_num)
    abs_array = np.absolute(dest_array - index_t)
    partition_index[i+1] = abs_array.argmin() + 1
    ## partition_index[i+1] = np.int32((adj_matrix_dim)*(i+1)/partition_num)
    ## partition_index[i+1] = np.int32(adj_matrix_dim*(math.pow(0.717, partition_num-1-i)))
partition_index[partition_num] = adj_matrix_dim
print("partition index array ", partition_index)


## multi process pool.
process_pool = Pool(partition_num)
result_pool = []
for i in range(partition_num):
    result_pool.append(process_pool.apply_async(func=partion_multi_process, args=(i, _shared_array, partition_index, adj_matrix_dim)))

process_pool.close()
process_pool.join()

graph_csr_row = []
graph_csr_col = []

for i in range(partition_num):
    temp_a, temp_b = result_pool[i].get()
    graph_csr_row.append(temp_a)
    graph_csr_col.append(temp_b)
    ## print(result_pool[i].get())

intersection_pool = Pool(partition_num)
final_result_pool = []
for i in range(partition_num):
    final_result_pool.append(intersection_pool.apply_async(func=intersection_multi_process, args=(i, _shared_array, graph_csr_row[i], graph_csr_col[i])))

intersection_pool.close()
intersection_pool.join()

result = 0
for i in range(partition_num):
    result += int(final_result_pool[i].get())

t_partition_end = perf_counter()
print("our code execution elapses ", t_partition_end - t_partition_start)
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

