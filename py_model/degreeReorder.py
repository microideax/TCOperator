import numpy as np
from collections import defaultdict

def read_graph_from_edge_list(file_path):
    edges = []
    with open(file_path, 'r') as file:
        for line in file:
            u, v = map(int, line.strip().split())
            edges.append((u, v))
    return edges

def generate_csr_files(graph_edges, offset_file_path, column_file_path):
    # Count the degrees of each vertex
    degrees = defaultdict(int)
    for u, v in graph_edges:
        degrees[u] += 1

    # Sort vertices based on degree in descending order
    sorted_vertices = sorted(degrees.keys(), key=lambda x: degrees[x], reverse=True)

    # Create the CSR data arrays
    num_vertices = len(sorted_vertices)
    num_edges = len(graph_edges)
    offsets = np.zeros(num_vertices + 1, dtype=np.uint32)
    columns = np.zeros(num_edges, dtype=np.uint32)

    # Fill the CSR data arrays
    current_vertex = 0
    current_edge = 0
    for vertex in sorted_vertices:
        offsets[current_vertex] = current_edge
        for u, v in graph_edges:
            if u == vertex:
                columns[current_edge] = v
                current_edge += 1
        current_vertex += 1
    offsets[current_vertex] = current_edge

    # Write CSR data to offset file
    with open(offset_file_path, 'w') as offset_file:
        for offset in offsets:
            offset_file.write(f"{offset}\n")

    # Write CSR data to column file
    with open(column_file_path, 'w') as column_file:
        for column in columns:
            column_file.write(f"{column}\n")

def save_reordered_edge_list(graph_edges, sorted_vertices, file_path):
    # Create a dictionary to store the mapping of old vertex IDs to new IDs
    vertex_mapping = {vertex: i for i, vertex in enumerate(sorted_vertices)}

    # Reorder the edge list based on the new vertex IDs
    reordered_edges = []
    for u, v in graph_edges:
        if u in vertex_mapping and v in vertex_mapping:
            reordered_edges.append((vertex_mapping[u], vertex_mapping[v]))

    # Save the reordered edge list to file
    with open(file_path, 'w') as edge_file:
        for u, v in reordered_edges:
            edge_file.write(f"{u} {v}\n")

# Example usage
edge_list_file = "dataset/amazon0601_edge_0.txt"
offset_file = "amazon0601_row.txt"
column_file = "amazon0601_col.txt"
reordered_edge_list_file = "amazon0601_edge_0.txt"

# Read graph from edge list file
graph_edges = read_graph_from_edge_list(edge_list_file)

# Count the degrees of each vertex
degrees = defaultdict(int)
for u, v in graph_edges:
    degrees[u] += 1

# Sort vertices based on degree in descending order
sorted_vertices = sorted(degrees.keys(), key=lambda x: degrees[x], reverse=True)

# Generate CSR format files with degree-based order
generate_csr_files(graph_edges, offset_file, column_file)

# Save reordered edge list
save_reordered_edge_list(graph_edges, sorted_vertices, reordered_edge_list_file)
