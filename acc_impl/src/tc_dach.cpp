// refactor on cache
#define __gmp_const const

#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <chrono>

#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>
#include <algorithm>
#include "ap_int.h"
#include "cache.h"

#define T               16
#define E_per_burst     8
#define N_offset        16
#define N_column        16

static const int N = 512;
typedef cache<ap_uint<512>, true, false, N_column, N, 4, 4, 1, false, 0, 0, false, 7> cache_512;
typedef cache<ap_uint<64>, true, false, N_offset, N, 8, 8, 8, false, 0, 0, false, 7> cache_64;

void loadEdgeList(int length, ap_uint<512>* inArr, hls::stream<ap_uint<512>>& eStrmOut) {
    int loop = (length + T - 1) / T;
    Load_edge: for (int i = 0; i < loop; i++) {
#pragma HLS pipeline ii = 1
        ap_uint<512> temp = inArr[i];
        eStrmOut << temp;
    }
}

void loadOffset(int length, cache_64& offset_list, hls::stream<ap_uint<512>>& eStrmIn, \
                hls::stream<ap_uint<512>>& StrmOff, hls::stream<ap_uint<512>>& StrmLen) {

    int loop = (length + T - 1) / T;

    ap_uint<512> edge_value;
    ap_uint<512> offset_value;
    ap_uint<512> length_value;

    Load_offset: for (int i = 0; i < loop; i++) {
#pragma HLS pipeline II=1
        edge_value = eStrmIn.read();
        for (int j = 0; j < E_per_burst*2; j++) { // we use 16 ports to set II = 1
#pragma HLS unroll
            int vertex = edge_value.range(32*(j+1) - 1, 32*j);
            ap_uint<64> offv = offset_list[vertex];
            int temp_o = offv.range(31, 0);
            int temp_l = offv.range(63, 32) - offv.range(31, 0);
            offset_value.range(32*(j+1) - 1, 32*j) = temp_o;
            length_value.range(32*(j+1) - 1, 32*j) = temp_l;
        }
        StrmOff << offset_value;
        StrmLen << length_value;
    }
}

void processList(int length, cache_512& column_list, 
                hls::stream<ap_uint<512>>& StrmOff,
                hls::stream<ap_uint<512>>& StrmLen, 
                int* tc_count) {

    int loop = (length + T - 1) / T;
    int tc_num = 0;

    ap_uint<512> offset_value;
    ap_uint<512> length_value;
    ap_uint<512> temp[T];

    Process_list: for (int i = 0; i < loop; i++) {
#pragma HLS pipeline II=1
        offset_value = StrmOff.read();
        length_value = StrmLen.read();
        for (int j = 0; j < T; j++) {
#pragma HLS unroll
            int temp_offset = offset_value.range(32*(j+1) - 1, 32*j);
            temp[j] = column_list[temp_offset]; // load adj list from global memory;

            // test function for intersection
            tc_num += temp[j].range(31, 0) & 0x1;
        }

//         intersection: for (int j = 0; j < E_per_burst; j++) {
// #pragma HLS unroll
//             for (int ii = 0; ii < T; ii++) {
//         #pragma HLS unroll
//                 for (int jj = 0; jj < T; jj++) {
//         #pragma HLS unroll
//                     tc_num += ((temp[j*2 + 1].range(32*(ii+1) - 1, 32*ii) > 0) & 
//                                (temp[j*2].range(32*(jj+1) - 1, 32*jj) > 0) &
//                                (temp[j*2 + 1].range(32*(ii+1) - 1, 32*ii) == temp[j*2].range(32*(jj+1) - 1, 32*jj)));
//                 }
//             }
//         }

    }
    tc_count[0] = tc_num;
    std::cout << tc_num << std::endl;
}


extern "C" {
void TriangleCount (ap_uint<512>* edge_list, ap_uint<64>* offset_list, ap_uint<512>* column_list, int edge_num, int* tc_number ) {

#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 32 max_read_burst_length = 32 bundle = gmem0 port = edge_list
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 32 max_read_burst_length = 32 bundle = gmem1 port = offset_list
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 32 max_read_burst_length = 32 bundle = gmem2 port = column_list
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 4 max_write_burst_length = 2 bundle = gmem0 port = tc_number

#pragma HLS INTERFACE s_axilite port = edge_list bundle = control
#pragma HLS INTERFACE s_axilite port = offset_list bundle = control
#pragma HLS INTERFACE s_axilite port = column_list bundle = control
#pragma HLS INTERFACE s_axilite port = edge_num bundle = control
#pragma HLS INTERFACE s_axilite port = tc_number bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS dataflow

    static hls::stream<ap_uint<512>> edgeStrm;
    static hls::stream<ap_uint<512>> offsetStrm;
    static hls::stream<ap_uint<512>> lengthStrm;

    int length = edge_num*2;
    cache_64 offset_cache(offset_list);
    cache_512 column_cache(column_list);
    loadEdgeList (length, edge_list, edgeStrm);
    cache_wrapper(loadOffset, length, offset_cache, edgeStrm, offsetStrm, lengthStrm);
    cache_wrapper(processList, length, column_cache, offsetStrm, lengthStrm, tc_number);
}
}
