#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>

// use 512 bit width to full utilize bandwidth
typedef struct v_datatype {int data[16];} int512;
#define BUF_DEPTH   512
#define T           16
#define T_offset    4
#define BURST_LEN   8
#define MAX_colase  16 // at this time , need to align with InSecNum;
#define InSecNum    4
const int c_size = BUF_DEPTH;

void adjListCpy(int dst[BUF_DEPTH][T], int512* src, int begin, int end) {
#pragma HLS inline
    for (int i = begin; i < end; i++) {
#pragma HLS pipeline
        int512 temp_a = src[i];
        for (int j = 0; j < T; j++) {
#pragma HLS unroll
            dst[i - begin][j] = temp_a.data[j];
        }
    }
}

void adjListCpyDst(int dst[InSecNum][T][BUF_DEPTH], int512* src, int begin, int end) {
#pragma HLS inline
    for (int i = begin; i < end; i++) {
#pragma HLS pipeline
        int512 temp_a = src[i];
        for (int j = 0; j < T; j++) {
#pragma HLS unroll
            for (int k = 0; k < InSecNum; k++) {
#pragma HLS unroll
                dst[k][j][i - begin] = temp_a.data[j];
            }
        }
    }
}

// since list b contains multiple adj lists, so need an extra offset.
void setIntersection(int list_a[T][BUF_DEPTH], int list_b[T][BUF_DEPTH], int list_a_offset, int list_a_len, \
                     int list_b_offset, int list_b_offset_t, int list_b_len_t, int* tc_count)
{
#pragma HLS inline
    int o_idx_a = list_a_offset & 0xf;
    int o_idx_b = (list_b_offset & 0xf) + (list_b_offset_t - list_b_offset);
    int idx_a = o_idx_a;
    int idx_b = o_idx_b;
    int count = 0;
loop_set_inetrsection: while ((idx_a < list_a_len + o_idx_a) && (idx_b < list_b_len_t + o_idx_b))
    {
#pragma HLS pipeline ii=1
        int a_dim_1 = idx_a >> T_offset;
        int a_dim_2 = idx_a & 0xf;

        int b_dim_1 = idx_b >> T_offset;
        int b_dim_2 = idx_b & 0xf;

        int value_a = list_a[a_dim_2][a_dim_1];
        int value_b = list_b[b_dim_2][b_dim_1];

        if(value_a < value_b)
            idx_a++;
        else if (value_a > value_b)
            idx_b++;
        else {
            count++;
            idx_a++;
            idx_b++;
        }
    }
    tc_count[0] = count;
}

template <typename DT>
void loadEdgelist(int length, DT* inArr, hls::stream<DT>& inStrm) {
    for (int i = 0; i < length; i++) {
#pragma HLS loop_tripcount min = c_size max = c_size
#pragma HLS pipeline ii = 1
        inStrm << inArr[i];
    }
}

template <typename DT>
void loadOffset(int* offset_list_1, int* offset_list_2, int length, \
                hls::stream<DT>& edgestrm_in, hls::stream<DT>& edgestrm_out, \
                hls::stream<DT>& a_idx_strm, hls::stream<DT>& b_idx_strm, \
                hls::stream<DT>& len_a_strm, hls::stream<DT>& len_b_strm){
    for(int i = 0; i < length; i++) {
        int a_value = edgestrm_in.read();
        int b_value = edgestrm_in.read();
        int a_idx = offset_list_1[a_value];
        int b_idx = offset_list_2[b_value];
        int len_a = offset_list_1[a_value +1] - a_idx;
        int len_b = offset_list_2[b_value +1] - b_idx;
        a_idx_strm << a_idx;
        b_idx_strm << b_idx;
        len_a_strm << len_a;
        len_b_strm << len_b;
        edgestrm_out << b_value;
    }
}

template <typename DT>
void colase_request (int edge_num, hls::stream<DT>& edge_strm, \
                        hls::stream<DT>& a_idx_strm, hls::stream<DT>& b_idx_strm, \
                        hls::stream<DT>& len_a_strm, hls::stream<DT>& len_b_strm,\ 
                        hls::stream<DT>& a_colase_idx_strm, hls::stream<DT>& a_colase_len_strm, \
                        hls::stream<DT>& b_colase_idx_strm, hls::stream<DT>& b_colase_len_strm, \
                        hls::stream<bool>& a_colase_strm, hls::stream<bool>& b_colase_strm, hls::stream<bool>& end_stream) {
    int a_idx_buffer = -1;
    int a_len_buffer = -1;
    int b_idx_buffer = -1; // only store the first idx offset
    int b_len_buffer[MAX_colase];
#pragma HLS array_partition variable=b_len_buffer type=complete
    int b_valid_idx = 0;
    int b_total_len = 0;
    bool colase_write = false;
    int b_current_value = -2;
    for (int i = 0; i < edge_num; i++) {
        int b_value = edge_strm.read();
        int a_idx = a_idx_strm.read();
        int b_idx = b_idx_strm.read();
        int a_len = len_a_strm.read();
        int b_len = len_b_strm.read();

        if ((a_idx != a_idx_buffer) || (b_valid_idx >= MAX_colase) || ((b_total_len + b_len) >= BUF_DEPTH * T)) {
            colase_write = true;
        }

        if (b_valid_idx > 0) {
            if (b_current_value != (b_value - 1)) {
                colase_write = true;
            }
        }

        if ((colase_write)&&(a_idx_buffer != -1)) { // remove initial case
            a_colase_idx_strm << a_idx_buffer;
            a_colase_len_strm << a_len_buffer;
            b_colase_idx_strm << b_idx_buffer;
            b_colase_len_strm << b_total_len;
            a_colase_strm << true;
            b_colase_strm << true;
            end_stream << false;

            b_colase_len_strm << b_valid_idx; // write a coalesce number
            for (int j = 0; j <= b_valid_idx - 1; j++ ) {
                b_colase_idx_strm << b_idx_buffer;
                b_colase_len_strm << b_len_buffer[j];
                a_colase_strm << false;
                b_colase_strm << false;
                end_stream << false;
                b_idx_buffer += b_len_buffer[j];
            }
            b_valid_idx = 0;
            b_total_len = 0;
        }

        a_idx_buffer = a_idx;
        a_len_buffer = a_len;
        b_idx_buffer = (b_valid_idx == 0)? b_idx : b_idx_buffer;
        b_len_buffer[b_valid_idx] = b_len;
        b_total_len += b_len;
        b_valid_idx += 1;
        b_current_value = b_value;

        colase_write = false;
    }
    // last edge, need write
    a_colase_idx_strm << a_idx_buffer;
    a_colase_len_strm << a_len_buffer;
    b_colase_idx_strm << b_idx_buffer;
    b_colase_len_strm << b_total_len;
    a_colase_strm << true;
    b_colase_strm << true;
    end_stream << false;

    b_colase_len_strm << b_valid_idx; // write a coalesce number
    for (int j = 0; j <= b_valid_idx - 1; j++ ) {
        b_colase_idx_strm << b_idx_buffer;
        b_colase_len_strm << b_len_buffer[j];
        a_colase_strm << false;
        b_colase_strm << false;
        end_stream << false;
        b_idx_buffer += b_len_buffer[j];
    }

    end_stream << true;
}

void proIntSecBina (int list_a[InSecNum][T][BUF_DEPTH], int list_b[InSecNum][T][BUF_DEPTH], \
                    int list_a_offset, int list_a_len, int list_b_offset, int list_b_num,  \
                    int b_idx[MAX_colase], int b_len[MAX_colase], int *count) {
#pragma HLS inline
    int triangle_count = 0;
    int temp_result[InSecNum] = {0};
#pragma HLS array_partition variable=temp_result dim=1 type=complete

    int loop_num = (list_b_num + InSecNum - 1) / InSecNum;

    proIntSec_loop: for (int i = 0; i < loop_num; i++) {
        proIntSec_Num: for (int j = 0; j < InSecNum; j++) {
#pragma HLS unroll
            temp_result[j] = 0;
            int inner_idx = i*InSecNum + j;
            int tri_count[1] = {0};
            setIntersection(list_a[j], list_b[j], list_a_offset, list_a_len, list_b_offset, b_idx[inner_idx], b_len[inner_idx], tri_count);
            
            if (inner_idx < list_b_num) {
                temp_result[j] = tri_count[0];
            }
        }
        for (int k = 0; k < InSecNum; k++) {
            triangle_count += temp_result[k];
        }
    }
    count[0] = triangle_count;
}

template <typename DT>
void procIntersec (int512* column_list_1, int512* column_list_2, int length, int* tri_count,
                    hls::stream<DT>& a_colase_idx_strm, hls::stream<DT>& a_colase_len_strm, \
                    hls::stream<DT>& b_colase_idx_strm, hls::stream<DT>& b_colase_len_strm, \
                    hls::stream<bool>& a_colase_strm, hls::stream<bool>& b_colase_strm, \
                    hls::stream<bool>& end_stream) {
    
    // int temp_count[1] = {0};
    int triangle_count = 0;
    int temp_result[InSecNum];
    int list_a [InSecNum][T][BUF_DEPTH];
    int list_b [InSecNum][T][BUF_DEPTH];
    // int list_b [BUF_DEPTH][T];
#pragma HLS array_partition variable=list_a type=complete dim=1 dim=2
#pragma HLS array_partition variable=list_b type=complete dim=1 dim=2
// #pragma HLS array_partition variable=list_b type=complete dim=2

    int list_a_len;
    int list_a_offset;
    int list_b_len;
    int list_b_offset;
    int list_a_previous = -1;

    bool end_flag = end_stream.read();
    while (!end_flag) {
        bool a_colase = a_colase_strm.read();
        bool b_colase = b_colase_strm.read();

        if (a_colase && b_colase) {
            // adj list cpy.
            list_a_len = a_colase_len_strm.read();
            list_a_offset = a_colase_idx_strm.read();
            list_b_len = b_colase_len_strm.read();
            list_b_offset = b_colase_idx_strm.read();

            int o_begin_a = list_a_offset >> T_offset;
            int o_end_a = (list_a_offset + list_a_len + T - 1) >> T_offset;
            int o_begin_b = list_b_offset >> T_offset;
            int o_end_b = (list_b_offset + list_b_len + T - 1) >> T_offset;

            if (list_a_previous != list_a_offset) { // a simple cache for list_a;
                adjListCpyDst(list_a, column_list_1, o_begin_a, o_end_a);
            }
            list_a_previous = list_a_offset;

            adjListCpyDst(list_b, column_list_2, o_begin_b, o_end_b);
            // adjListCpy(list_b, column_list_2, o_begin_b, o_end_b);

            int list_b_num = b_colase_len_strm.read(); // read a coalesce number
            int b_len[MAX_colase];
            int b_idx[MAX_colase];
#pragma HLS array_partition variable=b_len type=complete
#pragma HLS array_partition variable=b_idx type=complete

            for (int i = 0; i < MAX_colase; i++) {
                if (i < list_b_num) {
                    b_len[i] = b_colase_len_strm.read();
                    b_idx[i] = b_colase_idx_strm.read();
                    a_colase = a_colase_strm.read();
                    b_colase = b_colase_strm.read();
                    end_flag = end_stream.read();
                } else {
                    b_len[i] = 0;
                    b_idx[i] = list_b_offset;
                }
            }

            int count[1] = {0};
            proIntSecBina(list_a, list_b, list_a_offset, list_a_len, list_b_offset, list_b_num, b_idx, b_len, count);
            triangle_count += count[0];

//             int loop_num = (list_b_num + InSecNum - 1) / InSecNum;

//             for (int i = 0; i < loop_num; i++) {
//                 for (int j = 0; j < InSecNum; j++) {
// #pragma HLS unroll

//                     temp_result[j] = 0;
//                     int inner_idx = i*InSecNum + j;
//                     if (inner_idx < list_b_num) {
//                         int count[1] = {0};
//                         setIntersection(list_a[j], list_b[j], list_a_offset, list_a_len, list_b_offset, b_idx[inner_idx], b_len[inner_idx], count);
//                         temp_result[j] = count[0];
//                         // setIntersection(list_a, list_b, list_a_offset, list_a_len, list_b_offset, b_idx[i*InSecNum + j], b_len[i*InSecNum + j], count);
//                         // triangle_count += count[0];
//                     }
//                 }
//                 for (int k = 0; k < InSecNum; k++) {
//                     triangle_count += temp_result[k];
//                 }
//             }
        }

        end_flag = end_stream.read();
    }
    tri_count[0] = triangle_count;
}


extern "C" {
void TriangleCount(int* edge_list, int* offset_list_1, int* offset_list_2, int512* column_list_1, int512* column_list_2, int edge_num, int* tc_number ) {

#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem0 port = edge_list
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem1 port = offset_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem2 port = offset_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem3 port = column_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem4 port = column_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 32 num_read_outstanding = \
    1 max_write_burst_length = 2 max_read_burst_length = 2  bundle = gmem5 port = tc_number

#pragma HLS INTERFACE s_axilite port = edge_list bundle = control
#pragma HLS INTERFACE s_axilite port = offset_list_1 bundle = control
#pragma HLS INTERFACE s_axilite port = offset_list_2 bundle = control
#pragma HLS INTERFACE s_axilite port = column_list_1 bundle = control
#pragma HLS INTERFACE s_axilite port = column_list_2 bundle = control
#pragma HLS INTERFACE s_axilite port = edge_num bundle = control
#pragma HLS INTERFACE s_axilite port = tc_number bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS dataflow
// Initial local memory space of list_a and list_b
    // int512 list_a[BUF_DEPTH];
    // int512 list_b[BUF_DEPTH];

// local registers
    int edge_num_local = edge_num*2;

    static hls::stream<int> edgeInStrm("input_edge");
#pragma HLS STREAM variable = edgeInStrm depth=128
    static hls::stream<int> edgeOutStrm;
#pragma HLS STREAM variable = edgeOutStrm depth=64
    static hls::stream<int> a_idx_strm, b_idx_strm;
#pragma HLS STREAM variable = a_idx_strm depth=64
#pragma HLS STREAM variable = b_idx_strm depth=64
//     static hls::stream<int> a_idx_strm_o, b_idx_strm_o;
// #pragma HLS STREAM variable = a_idx_strm_o depth=16
// #pragma HLS STREAM variable = b_idx_strm_o depth=16
    static hls::stream<int> len_a_strm, len_b_strm;
#pragma HLS STREAM variable = len_a_strm depth=64
#pragma HLS STREAM variable = len_b_strm depth=64
//     static hls::stream<int> len_a_strm_o, len_b_strm_o;
// #pragma HLS STREAM variable = len_a_strm_o depth=16
// #pragma HLS STREAM variable = len_b_strm_o depth=16
    static hls::stream<int> a_colase_idx_strm, b_colase_idx_strm;
#pragma HLS STREAM variable = a_colase_idx_strm depth=64
#pragma HLS STREAM variable = b_colase_idx_strm depth=64
    static hls::stream<int> a_colase_len_strm, b_colase_len_strm;
#pragma HLS STREAM variable = a_colase_len_strm depth=64
#pragma HLS STREAM variable = b_colase_len_strm depth=64
    static hls::stream<bool> a_colase_strm, b_colase_strm;
#pragma HLS STREAM variable = a_colase_strm depth=64
#pragma HLS STREAM variable = b_colase_strm depth=64
    static hls::stream<bool> end_stream;
#pragma HLS STREAM variable = end_stream depth=64

    int triCount[1]={0};

    loadEdgelist<int>(edge_num_local, edge_list, edgeInStrm);
    loadOffset<int>(offset_list_1, offset_list_2, edge_num, edgeInStrm, edgeOutStrm, a_idx_strm, b_idx_strm, len_a_strm, len_b_strm);
    colase_request<int>(edge_num, edgeOutStrm, a_idx_strm, b_idx_strm, len_a_strm, len_b_strm,\ 
                        a_colase_idx_strm, a_colase_len_strm, b_colase_idx_strm, b_colase_len_strm, \
                        a_colase_strm, b_colase_strm, end_stream);
    procIntersec<int>(column_list_1, column_list_2, edge_num, triCount, \
                        a_colase_idx_strm, a_colase_len_strm, b_colase_idx_strm, b_colase_len_strm, \
                        a_colase_strm, b_colase_strm, end_stream);

    tc_number[0] = triCount[0];

}
}
