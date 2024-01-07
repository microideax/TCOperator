import argparse
import networkx as nx
from itertools import combinations
import numpy as np

def read_graph_from_file(file_path):
    edge_array = np.loadtxt(file_path, dtype=np.int32)
    # Sort the edge list by source vertex id, then by dest vertex id
    edges_sorted = edge_array[np.lexsort((edge_array[:, 1], edge_array[:, 0]))]
    # Remove duplicate edges
    unique_edges = np.unique(edges_sorted, axis=0)
    return unique_edges

def partition_graph(GraphInput, N):
    """Partition the graph with overlap regions."""
    source_vertices = np.unique(GraphInput[:, 0])
    partition_size  = (len(source_vertices) + N) // N
    partitions = []
    source_id = []
    dest_id = []
    
    for i in range(N):
        start_index = i * partition_size
        end_index = min((i + 1) * partition_size - 1, len(source_vertices))
        if start_index > end_index:
            partitions.append(np.array([]))
            source_id.append(np.array([]))
            dest_id.append(np.array([]))
            continue
            
        # Select the source vertices for this partition
        start_edge_index = np.searchsorted(GraphInput[:, 0], start_index, side='left')
        end_edge_index = np.searchsorted(GraphInput[:, 0], end_index, side='right')
        partition_vertices = np.array(GraphInput[start_edge_index:end_edge_index], dtype=np.int32)
        
        max_vertex_id = np.max(partition_vertices)
        
        if max_vertex_id > end_index:
            extend_index = np.searchsorted(GraphInput[:, 0], max_vertex_id, side='right')
            extend_array = np.array(GraphInput[end_edge_index:extend_index], dtype=np.int32)
            # print (start_index, end_index, max_vertex_id)
            # print (start_edge_index, end_edge_index, extend_index)
            # print (extend_array)
            partition_vertices = np.concatenate((partition_vertices, extend_array), axis=0)
        # Find the indices of the edges that belong to this partition
        # This is efficient since unique_edges is already sorted by source vertex
        partitions.append(np.array(partition_vertices))
        source_id.append(np.array(start_index))
        dest_id.append(np.array(end_index))
        print (np.array(partition_vertices))

    return partitions, source_id, dest_id

def triangle_count(edge_list, src, dst):
    G = nx.DiGraph()
    G.add_edges_from(edge_list)
    triangle = 0
    # Iterate over each vertex in the specified range
    for vertex in range(src, dst + 1):
        if vertex in G:
            neighbors = set(G.neighbors(vertex))
            # Iterate over pairs of neighbors to check for triangles
            for neighbor1 in neighbors:
                for neighbor2 in neighbors:
                    if neighbor1 != neighbor2 and G.has_edge(neighbor1, neighbor2):
                        # Found a triangle
                        triangle += 1
    # Each triangle is counted three times (once for each vertex), so divide by 3
    return triangle

def main():
    parser = argparse.ArgumentParser(description='Graph partitioning and triangle counting')
    parser.add_argument('file_path', type=str, help='Path to the graph dataset file')
    parser.add_argument('num_partitions', type=int, help='Number of partitions to divide the graph into')
    
    args = parser.parse_args()

    # Read the graph
    GraphSorted = read_graph_from_file(args.file_path + '.txt')
    print (GraphSorted)
    
    # Partition the graph
    partitions, source, dest = partition_graph(GraphSorted, args.num_partitions)
    
    count = 0
    for i in range(len(partitions)):
        count += triangle_count(partitions[i], source[i], dest[i])
    print ("our method: ", count)
    
    # NetworkX function to count triangles.
    G = nx.Graph()
    G.add_edges_from(GraphSorted)
    triangles = nx.triangles(G)
    total_triangle_count = sum(triangles.values())
    print ("networkx: ",total_triangle_count // 3)

if __name__ == "__main__":
    main()
