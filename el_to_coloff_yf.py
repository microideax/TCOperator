import numpy as np
from time import perf_counter
import sys


t_start = perf_counter()

n = len(sys.argv)

if n < 2:

    print("need input args")

    exit(-1)



file_name = sys.argv[1]

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
## print(txt_array)

t_loaded = perf_counter()

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

## print(list_b)
## print(txt_array)

vertex_max = np.int64(txt_array.max())

vertex_min = np.int64(txt_array.min())

print(vertex_max)

print(vertex_min)



edge_num = len(txt_array)

print(edge_num)

t_write_start = perf_counter()

f_offset = open(file_name + "_offset.txt", "wt")

f_column = open(file_name + "_column.txt", "wt")



f_offset.write(str(vertex_max + 1) + '\n')

f_offset.write(str(np.int64(0)) + '\n')

f_column.write(str(edge_num) + '\n')



a_offset = np.zeros(vertex_max + 1)

for i in range(0, edge_num):

    a_offset[txt_array[i][0]] = a_offset[txt_array[i][0]] + 1

    f_column.write(str(txt_array[i][1]) + '\n')



temp = 0

for i in range(0, vertex_max + 1):

    temp = temp + a_offset[i]

    f_offset.write(str(np.int64(temp)) + '\n')



f_offset.close()

f_column.close()

proc_end = perf_counter()

t_pro = proc_end - t_start
t_wr = proc_end - t_write_start
print("Processing time: ", t_pro)
print("File writing time: ", t_wr)
