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
                hls::stream<ap_uint<512>>& offStrm, hls::stream<ap_uint<512>>& lenStrm) {

    int loop = (length + T - 1) / T;

    ap_uint<512> edge_value;
    ap_uint<512> offset_value;
    ap_uint<512> length_value;

    Load_offset: for (int i = 0; i < loop; i++) {
#pragma HLS pipeline II=1
        edge_value = eStrmIn.read();
        for (int j = 0; j < T; j++) { // we use 16 ports to set II = 1
#pragma HLS unroll
            int vertex = edge_value.range(32*(j+1) - 1, 32*j);
            ap_uint<64> offv = offset_list[vertex];
            int temp_o = offv.range(31, 0);
            int temp_l = offv.range(63, 32) - offv.range(31, 0);
            offset_value.range(32*(j+1) - 1, 32*j) = temp_o;
            length_value.range(32*(j+1) - 1, 32*j) = temp_l;
        }
        offStrm << offset_value;
        lenStrm << length_value;
    }
}


/*
void filterFunc(int length, int j, \
                hls::stream<ap_uint<512>> offset_in, \
                hls::stream<ap_uint<512>> length_in, \
                hls::stream<ap_uint<512>> offset_out, \
                hls::stream<ap_uint<512>> length_out) {
    int loop = (length + T - 1) / T;
    filter_func: for (int i = 0; i < loop; i++) {
#pragma HLS pipeline II=1
        ap_uint<512> offset_temp = offset_in.read();
        ap_uint<512> length_temp = length_in.read();
        ap_uint<512> offset_o;
        ap_uint<512> length_o;
        int len = (length_temp.range(32*(j+1)-1, 32*j) == 0)? 1: 0; // if len == 0 , left shift.
        for (int index = 0; index < T; index++) {
    #pragma HLS unroll
            if ((index >= j) && ((index + len) < T)) {
                offset_o.range(32*(index+1)-1, 32*(index)) = offset_temp.range(32*(index+1+len)-1, 32*(index+len));
                length_o.range(32*(index+1)-1, 32*(index)) = length_temp.range(32*(index+1+len)-1, 32*(index+len));
            } else if ((index < j) && ((index + len) < T)) {
                offset_o.range(32*(index+1)-1, 32*(index)) = offset_temp.range(32*(index+1)-1, 32*index);
                length_o.range(32*(index+1)-1, 32*(index)) = length_temp.range(32*(index+1)-1, 32*index);
            } else {
                offset_o.range(32*(index+1)-1, 32*(index)) = 0;
                length_o.range(32*(index+1)-1, 32*(index)) = 0;
            }
        }
        offset_out << offset_o;
        length_out << length_o;
    }
}

void filterFuncLast(int length, \
                    hls::stream<ap_uint<512>> offset_in, \
                    hls::stream<ap_uint<512>> length_in, \
                    hls::stream<ap_uint<512>> offset_out, \
                    hls::stream<ap_uint<512>> length_out, \
                    hls::stream<bool> ctrl) {
    int loop = (length + T - 1) / T;
    filter_func_last: for (int i = 0; i < loop; i++) {
#pragma HLS pipeline II=1
        ap_uint<512> offset_temp = offset_in.read();
        ap_uint<512> length_temp = length_in.read();
        offset_out << offset_temp;
        length_out << length_temp;
        ctrl << true;
    }
    ctrl << false;
}

void filterStep1(int length, \
                hls::stream<ap_uint<512>>& offStrm, \
                hls::stream<ap_uint<512>>& lenStrm, \
                hls::stream<bool>& ctrlStrmTemp, \
                hls::stream<ap_uint<512>>& offStrmTemp, \
                hls::stream<ap_uint<512>>& lenStrmTemp) {

    hls::stream<ap_uint<512>> offset_temp[E_per_burst];
    hls::stream<ap_uint<512>> length_temp[E_per_burst];

    filterFunc(length, 0, offStrm, lenStrm, offset_temp[0], length_temp[0]);
    filter_function: for (int j = 1; j < E_per_burst - 1; j++) {
#pragma HLS unroll
        filterFunc(length, j, offset_temp[j-1], length_temp[j-1], \
                    offset_temp[j], length_temp[j]);
    }
    filterFuncLast(length, offset_temp[E_per_burst-2], length_temp[E_per_burst-2], \
                offStrmTemp, lenStrmTemp, ctrlStrmTemp);
}

void filterStep2(hls::stream<bool>& ctrlStrmTemp, \
                hls::stream<ap_uint<512>>& offStrmTemp, \
                hls::stream<ap_uint<512>>& lenStrmTemp
                hls::stream<bool>& ctrlStrm, \
                hls::stream<ap_uint<512>>& offStrm, \
                hls::stream<ap_uint<512>>& lenStrm) {
    
    int ring_length[64]; // 64 int size
    int ring_offset[64]; // 64 int size
    int read_addr = 0;
    int write_addr = 0;
    ap_uint<512> off_rd;
    ap_uint<512> len_rd;

    filter_step_2: while (1) {
#pragma HLS pipeline II=1
        bool ctrl = ctrlStrmTemp.read();
        if (ctrl == false) break;

        ap_uint<512> off_temp = offStrmTemp.read();
        ap_uint<512> len_temp = lenStrmTemp.read();

        for (int i = 0; i < T; i++) {
    #pragma HLS unroll
            bool write = (len_temp.range(32*(i+1)-1, 32*i) == 0)? false: true; 
            if (write) {
                ring_length[(write_addr + i)%(64)] = len_temp.range(32*(i+1)-1, 32*i);
                ring_offset[(write_addr + i)%(64)] = off_temp.range(32*(i+1)-1, 32*i);
                write_addr += 1;
            } else {
                ring_length[(write_addr + i)%(64)] = 0;
                ring_offset[(write_addr + i)%(64)] = 0;
            }
        }

        write_addr = write_addr % 64;
        if (((write_addr < read_addr) && (write_addr + 64 - read_addr > 16)) || (write_addr > (read_addr + 16))) {
        // read ring buffer
            for (int i = 0; i < T; i++) {
        #pragma HLS unroll
                off_rd.range(32*(i+1)-1, 32*i) = ring_offset[(read_addr + i)%(64)];
                len_rd.range(32*(i+1)-1, 32*i) = ring_length[(read_addr + i)%(64)];
                read_addr += 1;
            }
            read_addr = read_addr % 64;
            offStrm << off_rd;
            lenStrm << len_rd;
            ctrlStrm << false;
        }
    }

    // for remaining item
    for (int i = 0; i < T; i++) {
#pragma HLS unroll
        off_rd.range(32*(i+1)-1, 32*i) = ring_offset[(read_addr + i)%(64)];
        len_rd.range(32*(i+1)-1, 32*i) = ring_length[(read_addr + i)%(64)];
        read_addr += 1;
    }
    ctrlStrm << true;
}
*/

void loadAdjList(int length, cache_512& column_cache, \
                 hls::stream<ap_uint<512>>& offsetStrm, \
                 hls::stream<ap_uint<512>>& lengthStrm, int* tc_number) {
    int tc_num = 0;
    int loop = (length + T - 1) / T;

    load_adj_list: for (int i = 0; i < loop; i++) {
#pragma HLS pipeline II=1
        ap_uint<512> offset_out = offsetStrm.read();
        ap_uint<512> length_out = lengthStrm.read();

        for (int i = 0; i < T; i++) {
    #pragma HLS unroll
            int value_offset = offset_out.range(32*(i+1) - 1, 32*i);
            int edge_idx = i >> 1;
            int src_length = length_out.range(32*(edge_idx*2+1) - 1, 32*edge_idx*2);
            int dst_length = length_out.range(32*(edge_idx*2+2) - 1, 32*(edge_idx*2+1));

            ap_uint<512> value_temp;
            if ((src_length == 0) || (src_length == 0)) {
                value_temp = 0;
                // std::cout << "len = 0" <<std::endl;
            } else {
                value_temp = column_cache[value_offset];
            }

            // just for test
            tc_num += value_temp.range(31, 0);
        }
    }
    tc_number[0] = tc_num;
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


void triangleCount(int length, ap_uint<512>*  edge_list, cache_64& offset_cache, cache_512& column_cache, int* tc_number) {

    static hls::stream<ap_uint<512>> eStrmOut;
    static hls::stream<ap_uint<512>> offsetStrm;
    static hls::stream<ap_uint<512>> lengthStrm;
    // static hls::stream<ap_uint<512>> offsetStrmTemp;
    // static hls::stream<ap_uint<512>> lengthStrmTemp;
    // static hls::stream<bool>         ctrlStrmTemp;
    // static hls::stream<ap_uint<512>> offsetStrmOut;
    // static hls::stream<ap_uint<512>> lengthStrmOut;
    // static hls::stream<bool>         ctrlStrm;    

    // load edge
    loadEdgeList(length, edge_list, eStrmOut);
    loadOffset(length, offset_cache, eStrmOut, offsetStrm, lengthStrm);
    // filterStep1(length, offsetStrm, lengthStrm, ctrlStrmTemp, offsetStrmTemp, lengthStrmTemp);
    // filterStep2(ctrlStrmTemp, offsetStrmTemp, lengthStrmTemp, ctrlStrm, offsetStrmOut, lengthStrmOut);
    loadAdjList(length, column_cache, offsetStrm, lengthStrm, tc_number);
    // processList(); need to add intersection in the future.
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

    int length = edge_num*2;
    cache_64 offset_cache(offset_list);
    cache_512 column_cache(column_list);
    cache_wrapper(triangleCount, length, edge_list, offset_cache, column_cache, tc_number);
}
}
