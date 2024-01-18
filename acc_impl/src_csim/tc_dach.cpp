// refactor on cache
#define __gmp_const const

#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>

#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>
#include <algorithm>
#include "ap_int.h"
#include "cache.h"

#define T               16
#define E_per_burst     8
#define T_offset        4
#define Parallel        8

typedef struct data_64bit_type {int data[2];} int64;
typedef struct data_512bit_type {int data[16];} int512;
typedef struct data_custom_type {int offset[Parallel]; int length[Parallel];} para_int;

static const int N = 512;
typedef cache<int512, true, false, 1, N, 8, 8, 1, false, 8, 8, false, 3> cache_512;
typedef cache<int64, true, false, 2, N, 64, 8, 1, false, 64, 8, false, 3, BRAM, BRAM> cache_64;
// typedef cache<int64, true, true, 1, N, 2, 1, 8, true, 0, 0, false, 2> cache_64;

void loadEdgeList(int length, int512* inArr, hls::stream<int512>& eStrmOut) {
    int loop = (length + T - 1) / T;
    for (int i = 0; i < loop; i++) {
#pragma HLS pipeline ii = 1
        eStrmOut << inArr[i];
    }
}

void loadOffset(int length, cache_64& offset_list, hls::stream<int512>& eStrmIn, \
                hls::stream<bool>& ctrlStrm, hls::stream<para_int>& StrmA, hls::stream<para_int>& StrmB) {

    int loop = (length + T - 1) / T;
    int count = 0; // for edge filter

    para_int a_para;
    para_int b_para;
#pragma HLS DATA_PACK variable=a_para
#pragma HLS ARRAY_PARTITION variable=a_para.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=a_para.length complete dim=1
#pragma HLS DATA_PACK variable=b_para
#pragma HLS ARRAY_PARTITION variable=b_para.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_para.length complete dim=1

    int512 edge_value;
    int a_value, b_value;
    int a_offset, a_length;
    int b_offset, b_length;

    for (int i = 0; i < loop; i++) {
        edge_value = eStrmIn.read();
#pragma HLS array_partition variable=edge_value type=complete dim=1

        for (int j = 0; j < E_per_burst; j++) { // we use two ports to set II = 1
#pragma HLS pipeline II=1
            if ((i*T + j*2) < length) {
                a_value = edge_value.data[j*2];
                int64 temp_a = offset_list[a_value];
                a_offset = temp_a.data[0];
                a_length = temp_a.data[1] - temp_a.data[0];

                b_value = edge_value.data[j*2 + 1];
                int64 temp_b = offset_list[b_value];
                b_offset = temp_b.data[0];
                b_length = temp_b.data[1] - temp_b.data[0];

                if ((a_length > 0) && (b_length > 0)) {
                    a_para.offset[count] = a_offset;
                    a_para.length[count] = a_length;
                    b_para.offset[count] = b_offset;
                    b_para.length[count] = b_length;
                    count ++;
                    if (count == Parallel) {
                        count = 0;
                        StrmA << a_para;
                        StrmB << b_para;
                        ctrlStrm << true; // not the end stream
                    }
                }
            }
        }
    }

    for (int i = count; i < Parallel; i++) {
        a_para.offset[i] = -1;
        a_para.length[i] = 0;
        b_para.offset[i] = -1;
        b_para.length[i] = 0;
    }
    StrmA << a_para;
    StrmB << b_para;
    ctrlStrm << true; // not the end stream
    StrmA << a_para;
    StrmB << b_para;
    ctrlStrm << false; // end stream
}


extern "C" {
void TriangleCount (int512* edge_list, int64* offset_list, int512* column_list_1, int512* column_list_2, int edge_num, int* tc_number ) {

#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 32 max_read_burst_length = 32 bundle = gmem0 port = edge_list
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 32 bundle = gmem1 port = offset_list
#pragma HLS INTERFACE m_axi offset = slave latency = 4 num_read_outstanding = 16 bundle = gmem2 port = column_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 4 num_read_outstanding = 16 bundle = gmem3 port = column_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 4 bundle = gmem0 port = tc_number

#pragma HLS INTERFACE s_axilite port = edge_list bundle = control
#pragma HLS INTERFACE s_axilite port = offset_list bundle = control
#pragma HLS INTERFACE s_axilite port = column_list_1 bundle = control
#pragma HLS INTERFACE s_axilite port = column_list_2 bundle = control
#pragma HLS INTERFACE s_axilite port = edge_num bundle = control
#pragma HLS INTERFACE s_axilite port = tc_number bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS dataflow

    static hls::stream<int512> edgeStrm;
#pragma HLS STREAM variable = edgeStrm depth=32
    static hls::stream<bool> ctrlStrm;
#pragma HLS STREAM variable = ctrlStrm depth=32
    static hls::stream<para_int> offLenStrmA;
#pragma HLS STREAM variable = offLenStrmA depth=32
    static hls::stream<para_int> offLenStrmB;
#pragma HLS STREAM variable = offLenStrmB depth=32

    int length = edge_num*2;
    cache_64 offset_cache(offset_list);
    loadEdgeList (length, edge_list, edgeStrm);
    cache_wrapper(loadOffset, length, offset_cache, edgeStrm, ctrlStrm, offLenStrmA, offLenStrmB);

    bool strm_control;

    pp_load_cpy_process: while(1) {
#pragma HLS pipeline
        strm_control = ctrlStrm.read();
        para_int a_ele_in = offLenStrmA.read();
        para_int b_ele_in = offLenStrmB.read();

        if (strm_control == false) {
            break;
        }
    }

    printf("offset_cache hit ratio = \n");
	for (auto port = 0; port < 2; port++) {
		printf("\tP=%d: L1=%d/%d; L2=%d/%d\n", port,
				offset_cache.get_n_l1_hits(port), offset_cache.get_n_l1_reqs(port),
				offset_cache.get_n_hits(port), offset_cache.get_n_reqs(port));
	}

}
}

bool fileExists(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();
}

int getPartitionSize(std::string dataset_name) {
    for (int i = 0; i < 10000; i++){
        std::string file_name = "/home/yufeng/TCOperator/acc_impl/src_csim/../dataset_adj_test/" + dataset_name + "_col_" + std::to_string(i) + ".txt";
        if (fileExists(file_name) == false) {
            return i;
        }
    }
    return 0;
}

void getTxtSize (std::string file_name, int& lineNum) {
    // std::cout << "Getting file size ... " << file_name;
    std::fstream file;
    int line_num = 0;
    file.open(file_name, std::ios::in);
    if (file.is_open()) {
        std::string temp;
        while(getline(file, temp)) {
            line_num ++;
        }
    }
    file.close();
    lineNum = line_num;
    // std::cout << " " << lineNum << std::endl;
}

void getTxtContent (std::string file_name, int* array, int lineNum, bool is_edge) {
    // std::cout << "Getting file content ... " << file_name << std::endl;
    std::fstream file;
    file.open(file_name, std::ios::in);
    if (file.is_open()) {
        std::string tp, s_tp;
        int i = 0, line = 0;
        int data_t[2];
        while(getline(file, tp)) {
            if (is_edge == true) {
                std::stringstream ss(tp);
                while (getline(ss, s_tp, ' ')) {
                    data_t[i] = stoi(s_tp);
                    i = (i + 1) % 2;
                }
                array[line*2] = data_t[0];
                array[line*2 + 1] = data_t[1];
                line++;
            } else {
                array[line] = stoi(tp);
                line++;
            }
        }
        file.close(); // close the file object.
        if (line != lineNum) { // double check
            std::cout << "[ERROR] read file not align " << std::endl;
        }
    } else {
        std::cout << "[ERROR] can not open file " << file_name << std::endl;
    }
}


int main(int argc, char** argv) {

    std::string datasetName = "facebook_combined";
    int device_index = 0;

    int partition_number = getPartitionSize(datasetName);
    std::cout << "load graph dataset " << datasetName << ", partition number = " << partition_number << std::endl;

    int TcNum = 0;
    double Kernel_exe_time = 0;

    // for (int p_idx = 0; p_idx < partition_number; p_idx++) {
    for (int p_idx = 161; p_idx < 164; p_idx++) {
	    std::cout << "partition id = " << p_idx << std::endl;
        int edgeNum;
        std::string edgeName = "/home/yufeng/TCOperator/acc_impl/src_csim/../dataset_adj_test/" + datasetName + "_edge.txt";
        // std::string edgeName = "./dataset_test/edge.txt";
        getTxtSize(edgeName, edgeNum);

        int columnNum, offsetNum;
        std::string offsetName = "/home/yufeng/TCOperator/acc_impl/src_csim/../dataset_adj_test/" + datasetName + "_row_align_" + std::to_string(p_idx) + ".txt";
        std::string columnName = "/home/yufeng/TCOperator/acc_impl/src_csim/../dataset_adj_test/" + datasetName + "_col_align_" + std::to_string(p_idx) + ".txt";
        // std::string offsetName = "./dataset_test/row_double.txt";
        // std::string columnName = "./dataset_test/col.txt";

        getTxtSize(offsetName, offsetNum);
        getTxtSize(columnName, columnNum);

        // std::cout << " allocate memory in device" << std::endl;

        // int edge_size_bytes = edgeNum * 2 * sizeof(int);
        // xrt::bo edgeBuffer = xrt::bo(device, edge_size_bytes, tc_krnl.group_id(0));
        // int* edgeList = edgeBuffer.map<int*>();
        int* edgeList = new int [edgeNum*2]; 
        getTxtContent(edgeName, edgeList, edgeNum, true);

        // xrt::bo columnBuffer = xrt::bo(device, (columnNum * sizeof(int)), tc_krnl.group_id(2));
        // xrt::bo offsetBuffer = xrt::bo(device, (offsetNum * sizeof(int)), tc_krnl.group_id(1));
        // int* columnList = columnBuffer.map<int*>();
        // int* offsetList = offsetBuffer.map<int*>();
        int* columnList = new int [columnNum];
        int* offsetList = new int [offsetNum];
        getTxtContent(columnName, columnList, columnNum, false);
        getTxtContent(offsetName, offsetList, offsetNum, false);

        // xrt::bo resultBuffer = xrt::bo(device, sizeof(int), tc_krnl.group_id(0));
        // int* result = resultBuffer.map<int*>();
        int* result = new int[1];
        result[0] = 0;

        auto start = std::chrono::steady_clock::now();

        int512* int512_edge = (int512*)edgeList;
        int64* int64_offset = (int64*)offsetList;
        int512* int512_column = (int512*)columnList;
        TriangleCount(int512_edge, int64_offset, int512_column, int512_column, edgeNum, result);

        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end-start;

        TcNum += result[0];
        Kernel_exe_time += elapsed_seconds.count();

        std::cout << "Partition id = " << p_idx << " TC number = " << result[0] << " Time elapse " << elapsed_seconds.count() << std::endl;

        delete[] edgeList;
        delete[] columnList;
        delete[] offsetList;
    }

    std::cout << "Overall TC number = " << TcNum << std::endl;
    std::cout << "Overall kernel time = " << Kernel_exe_time << std::endl;
    return 0;
}
