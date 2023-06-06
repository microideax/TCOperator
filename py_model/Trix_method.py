import networkx as nx
import numpy as np
from time import perf_counter

def list_set_intersection (list_first, list_second) :
    ## this method is too slow
    ## for i in range(len(list_first)):
        ## count += list_first[i] & list_second[i]
    ## print(count)
    count = np.bincount(list_first + list_second)
    ## print(count)
    if (len(count) == 3):
        return count[2]
    else :
        return 0

file_name = './datasets/facebook_combined.txt'

#================================= graph preprocess =================================#

txt_array_t = np.int64(np.loadtxt(file_name))
txt_array = txt_array_t[:,:2]

## if source_id > dest_id, exchange them; only suitable for undirected graph.
for i in range (txt_array.shape[0]) :
    if (txt_array[i][0] >= txt_array[i][1]) :
        temp = txt_array[i][1]
        txt_array[i][1] = txt_array[i][0]
        txt_array[i][0] = temp

txt_array = txt_array[np.lexsort([txt_array.T[1]])] ## sort array by incremental order
txt_array = txt_array[np.lexsort([txt_array.T[0]])] ## sort array by incremental order

list_a = [] ## delete self-edges
for i in range (txt_array.shape[0]) :
    if (txt_array[i][0] == txt_array[i][1]) :
        list_a.append(i)
## print(list_a)
## print(txt_array)
txt_array = np.delete(txt_array, list_a, axis = 0)

list_b = []  ## delete duplicated edges 
for i in range (1, txt_array.shape[0]) :
    if ((txt_array[i][0] == txt_array[i-1][0]) & (txt_array[i][1] == txt_array[i-1][1])) :
        list_b.append(i)
## print(list_a)
txt_array = np.delete(txt_array, list_b, axis = 0)

vertex_max = np.int64(txt_array.max())
vertex_min = np.int64(txt_array.min())

## convert array to adjcent matrix

print (vertex_max)
print (vertex_min)
print (txt_array.shape[0])

#================================= graph partition =================================#
## need to add csr format for memory efficiency !! 

## create adj matrix

adj_matrix = np.zeros([vertex_max+1, vertex_max+1], dtype = int) 
for i in range (txt_array.shape[0]):
    ## print (txt_array[i])
    adj_matrix[txt_array[i][0]][txt_array[i][1]] = 1

print (adj_matrix)

## split adj matrix vertically

split_index = int(vertex_max/2)  ## define two vertical partitions
index_vertical = [0, split_index, vertex_max]

adj_partition_1 = adj_matrix[:, : split_index + 1] 
adj_partition_2 = adj_matrix[:, split_index + 1: vertex_max+1] 

print(adj_partition_1)
print(adj_partition_2)

#================================= TC operation in partition =================================#

tri_count_1 = 0
tri_count_2 = 0
for i in range (txt_array.shape[0]):
    edge_a = txt_array[i][0]
    edge_b = txt_array[i][1]
    temp_1 = list_set_intersection(adj_partition_1[edge_a], adj_partition_1[edge_b])
    ## print("temp1", temp_1)
    tri_count_1 += temp_1

for i in range (txt_array.shape[0]):
    edge_a = txt_array[i][0]
    edge_b = txt_array[i][1]
    temp_2 = list_set_intersection(adj_partition_2[edge_a], adj_partition_2[edge_b])
    ## print("temp2", temp_2)
    tri_count_2 += temp_2

print(tri_count_1)
print(tri_count_2)
result_partition = tri_count_1+ tri_count_2
print(result_partition)


#================================= GOLDEN TEST =================================#

testgraph = nx.read_edgelist(file_name, comments='#', delimiter=None, create_using=None, nodetype=None, data=True, edgetype=None, encoding='utf-8')
result_golden = int(sum(nx.triangles(testgraph).values())/3)
print(result_golden)

if (result_partition == result_golden) :
    print("TEST PASS")
else :
    print("TEST FAILED")
