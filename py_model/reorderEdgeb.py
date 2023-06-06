import argparse
from collections import defaultdict, deque

def reorder_edges(input_file_path, output_file_path):
    # Read edges from file
    with open(input_file_path, 'r') as input_file:
        edges = [tuple(map(int, line.strip().split())) for line in input_file]

    # Group edges by 'b' value
    edge_groups = defaultdict(list)
    for a, b in edges:
        edge_groups[b].append((a, b))

    # Create a list to hold the reordered edges
    reordered_edges = []

    # Create a queue to hold the current group of edges
    current_group = deque(maxlen=8)

    # Iterate over the edge groups in order of 'b' value
    for b, group in sorted(edge_groups.items()):
        for edge in group:
            # Add the edge to the current group
            current_group.append(edge)

            # If the current group is full, add it to the reordered edges
            if len(current_group) == 8:
                reordered_edges.extend(current_group)
                current_group.clear()

    # Add any remaining edges in the current group to the reordered edges
    reordered_edges.extend(current_group)

    # Write reordered edges to file
    with open(output_file_path, 'w') as output_file:
        for a, b in reordered_edges:
            output_file.write(f"{a} {b}\n")

def main():
    parser = argparse.ArgumentParser(description='Reorder edges in a file to maximize the reuse of "b" values within each group of 8 edges.')
    parser.add_argument('-i', '--input', required=True, help='Path to the input edge list file')
    parser.add_argument('-o', '--output', required=True, help='Path to the output file where reordered edges will be written')

    args = parser.parse_args()

    reorder_edges(args.input, args.output)

if __name__ == '__main__':
    main()
