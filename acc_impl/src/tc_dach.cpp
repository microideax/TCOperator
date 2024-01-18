// refactor on cache
#define __gmp_const const

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
typedef cache<int512, true, true, 1, N, 2, 1, 8, true, 0, 0, false, 2> cache_512;
typedef cache<int64, true, false, 2, N, 64, 8, 1, false, 64, 8, false, 3, BRAM, BRAM> cache_64;

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

        Load_offset: for (int j = 0; j < E_per_burst; j++) { // we use two ports to set II = 1
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
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 32 max_read_burst_length = 2 max_write_burst_length = 2 bundle = gmem1 port = offset_list
#pragma HLS INTERFACE m_axi offset = slave latency = 4 num_read_outstanding = 16 max_read_burst_length = 2 max_write_burst_length = 2 bundle = gmem2 port = column_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 4 num_read_outstanding = 16 max_read_burst_length = 2 max_write_burst_length = 2 bundle = gmem3 port = column_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 4 max_write_burst_length = 2 bundle = gmem0 port = tc_number

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
}
}
