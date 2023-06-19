# # Please generate the function that can load a log file and count the number of lines with content "Loading list b"
# # The function should return the number of lines
# import argparse

# def count_loading_list_b_lines(log_file_path):
#     with open(log_file_path, 'r') as log_file:
#         count_list_a = sum(1 for line in log_file if "Loading list a" in line)
#     with open(log_file_path, 'r') as log_file:
#         count_list_b = sum(1 for line in log_file if "Loading list b" in line)
#     with open(log_file_path, 'r') as log_file:
#         overlap = sum(1 for line in log_file if "list b overlap" in line)
#     return count_list_a, count_list_b, overlap

# def main():
#     parser = argparse.ArgumentParser(description='Count the number of lines containing "Loading list b" in a log file.')
#     parser.add_argument('-l', '--log', required=True, help='Path to the log file')

#     args = parser.parse_args()

#     count_a, count_b, overlap = count_loading_list_b_lines(args.log)

#     print("loaded number of list a: ", count_a)
#     print("loaded number of list b: ", count_b)
#     print("Overlap of list b: ", overlap)

# if __name__ == '__main__':
#     main()

import argparse

def count_loading_list_b_lines_and_sum_overlap_counts(log_file_path):
    loading_list_b_count = 0
    overlap_count_sum = 0

    with open(log_file_path, 'r') as log_file:
        for line in log_file:
            if "load offset values" in line:
                loading_list_b_count += 1
            if line.startswith("Overlap count:"):
                overlap_count = int(line.split(":")[1].strip())
                overlap_count_sum += overlap_count

    return loading_list_b_count, overlap_count_sum

def main():
    parser = argparse.ArgumentParser(description='Count the number of lines containing "Loading list b" and sum the overlap counts in a log file.')
    parser.add_argument('-l', '--log', required=True, help='Path to the log file')

    args = parser.parse_args()

    loading_list_b_count, overlap_count_sum = count_loading_list_b_lines_and_sum_overlap_counts(args.log)

    print(f'Number of lines containing "Loading list b": {loading_list_b_count}')
    print(f'Sum of overlap counts: {overlap_count_sum}')

if __name__ == '__main__':
    main()
