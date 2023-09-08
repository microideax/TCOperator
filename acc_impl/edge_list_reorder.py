from collections import Counter
import numpy as np

# Read the text file into a NumPy array
dataset_name = 'facebook_combined'
data = np.loadtxt('./dataset_1pe/' + dataset_name + '_edge_0.txt', dtype = np.uint32)

# Print the NumPy array
# print(data)

# Get the frequency of the first column items
frequency = Counter(item[0] for item in data)

# Print the frequency
reserve_symbol = False
idx = 0
for item, count in frequency.items():
    ## print(f"Item {item} appears {count} times")
    index_begin = idx
    index_end = idx + count
    ## print (index_begin, index_end)
    sorted_array = np.array(sorted(data[index_begin:index_end], key=lambda x: x[1], reverse=reserve_symbol))
    data[index_begin: index_end] = sorted_array
    ## print (sorted_array)

    idx = idx + count
    reserve_symbol = not reserve_symbol

# print(data)
np.savetxt("./dataset_1pe/" + dataset_name + "_edge_reorder_0.txt", data, fmt='%d', delimiter=' ')
