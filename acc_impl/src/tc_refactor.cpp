// tc_refactor file
#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>

// use 512 bit width to full utilize bandwidthedgeStrm
typedef struct data_512bit_type {int data[16];} int512;
typedef struct data_256bit_type {int data[8];} int256;
#define BUF_DEPTH   512
#define T           16
#define T_offset    4
#define T_2         8

void loadEdgeList(int length, int512* inArr, hls::stream<int512>& eStrmOut) {
    int loop = (length + T - 1) / T;
    for (int i = 0; i < loop; i++) {
#pragma HLS pipeline ii = 1
        eStrmOut << inArr[i];
    }
    eStrmOut << inArr[loop - 1]; // for ping-pong iteration
}

void loadOffset(int length, int* offset_list_1, int* offset_list_2, 
                hls::stream<int512>& eStrmIn, hls::stream<int512>& outEdgeStrm,
                hls::stream<int256>& offsetStrmA, hls::stream<int256>& offsetStrmB, 
                hls::stream<int256>& lengthStrmA, hls::stream<int256>& lengthStrmB ) {
    int loop = (length + T - 1) / T;
    for (int i = 0; i <= loop; i++) {
        int512 edge_value = eStrmIn.read();
#pragma HLS array_partition variable=edge_value type=complete dim=1
        int256 length_value_a;
        int256 offset_value_a;
#pragma HLS array_partition variable=length_value_a type=complete dim=1
#pragma HLS array_partition variable=offset_value_a type=complete dim=1
        int256 length_value_b;
        int256 offset_value_b;
#pragma HLS array_partition variable=length_value_b type=complete dim=1
#pragma HLS array_partition variable=offset_value_b type=complete dim=1
        for (int j = 0; j < T_2; j++) {
#pragma HLS pipeline
            if (i*T + 2*j < length) {
                int a_value = edge_value.data[j*2];
                int a_offset = offset_list_1[a_value];
                int a_length = offset_list_1[a_value + 1] - a_offset;
                length_value_a.data[j] = a_length;
                offset_value_a.data[j] = a_offset;
                int b_value = edge_value.data[j*2 + 1];
                int b_offset = offset_list_2[b_value];
                int b_length = offset_list_2[b_value + 1] - b_offset;
                length_value_b.data[j] = b_length;
                offset_value_b.data[j] = b_offset;
            }
            else {  
                length_value_a.data[j] = 0;
                offset_value_a.data[j] = 0;
                length_value_b.data[j] = 0;
                offset_value_b.data[j] = 0;
                edge_value.data[2*j] = -2;
                edge_value.data[2*j + 1] = -2;
            }
        }
        offsetStrmA << offset_value_a;
        lengthStrmA << length_value_a;
        offsetStrmB << offset_value_b;
        lengthStrmB << length_value_b;
        outEdgeStrm << edge_value;
    }
}

void loadCpyListA ( int512 edgeStrmIn_value, int512* column_list_1, 
                    int256 offsetStrmA_value, int256 lengthStrmA_value, 
                    int list_a_cache[1][T][BUF_DEPTH], int list_a_tag[2], 
                    int list_a[T_2][T][BUF_DEPTH], int256 offsetValueA[1], int256 lengthValueA[1]) {

    int512 edge_value;
#pragma HLS array_partition variable=edge_value type=complete dim=1
    int256 offset_value;
    int256 length_value;
#pragma HLS array_partition variable=offset_value type=complete dim=1
#pragma HLS array_partition variable=length_value type=complete dim=1

    edge_value = edgeStrmIn_value;
    offset_value = offsetStrmA_value;
    length_value = lengthStrmA_value;

    load_copy_list_a_T_2: for (int j = 0; j < T_2; j++) {
        if (edge_value.data[2*j] == list_a_tag[0]) { // list_a_tag[0]: value
            // hit, copy data from list_a cache
            copy_list_a: for (int ii = 0; ii < list_a_tag[1]; ii++) { // list_a_tag[1]: length
#pragma HLS pipeline
                for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                    list_a[j][jj][ii] = list_a_cache[0][jj][ii];
                }
            }
        } else {
            // miss, load data from off-chip memory and update cache
            int o_begin_a = offset_value.data[j] >> T_offset;
            int o_end_a = (offset_value.data[j] + length_value.data[j] + T - 1) >> T_offset;
            load_list_a: for (int ii = o_begin_a; ii < o_end_a; ii++) {
#pragma HLS pipeline
                int512 list_a_temp = column_list_1[ii];
                for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                    list_a[j][jj][ii - o_begin_a] = list_a_temp.data[jj];
                    list_a_cache[0][jj][ii - o_begin_a] = list_a_temp.data[jj];
                }
            }
            list_a_tag[0] = edge_value.data[2*j];
            list_a_tag[1] = o_end_a - o_begin_a;
        }
    }

    offsetValueA[0] = offset_value;
    lengthValueA[0] = length_value;
}

void loadListB (int256 offsetStrmB_value, int256 lengthStrmB_value, int512* column_list_2,
                int list_b[T][T][BUF_DEPTH], int256 offsetValueB[1], int256 lengthValueB[1]) {

    int256 offset_value;
    int256 length_value;
#pragma HLS array_partition variable=offset_value type=complete dim=1
#pragma HLS array_partition variable=length_value type=complete dim=1

    offset_value = offsetStrmB_value;
    length_value = lengthStrmB_value;

    // load list_b
    load_list_b_T_2: for (int j = 0; j < T_2; j++) {
        // TODO: add coalesce
        int o_begin_b = offset_value.data[j] >> T_offset;
        int o_end_b = (offset_value.data[j] + length_value.data[j] + T - 1) >> T_offset;
        load_list_b: for (int ii = o_begin_b; ii < o_end_b; ii++) {
#pragma HLS pipeline
            int512 list_b_temp = column_list_2[ii];
            for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                list_b[j][jj][ii - o_begin_b] = list_b_temp.data[jj];
            }
        }
    }

    offsetValueB[0] = offset_value;
    lengthValueB[0] = length_value;
}

void setIntersection(int list_a[T][BUF_DEPTH], int list_b[T][BUF_DEPTH], int list_a_offset, int list_a_len, \
                     int list_b_offset, int list_b_len, int* tc_count)
{
#pragma HLS inline
    int o_idx_a = list_a_offset & 0xf;
    int o_idx_b = list_b_offset & 0xf;
    int idx_a = o_idx_a;
    int idx_b = o_idx_b;
    int count = 0;  
loop_set_insection: while ((idx_a < list_a_len + o_idx_a) && (idx_b < list_b_len + o_idx_b))
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

void processList(int list_a[T_2][T][BUF_DEPTH], int list_b[T_2][T][BUF_DEPTH], 
                 int256 offsetValueA[1], int256 offsetValueB[1],
                 int256 lengthValueA[1], int256 lengthValueB[1], int* tc_count) {
#pragma HLS inline
    int tri_count = 0;

    int256 offset_value_a = offsetValueA[0];
    int256 length_value_a = lengthValueA[0];
#pragma HLS array_partition variable=offset_value_a type=complete
#pragma HLS array_partition variable=length_value_a type=complete
    int256 offset_value_b = offsetValueB[0];
    int256 length_value_b = lengthValueB[0];
#pragma HLS array_partition variable=offset_value_b type=complete
#pragma HLS array_partition variable=length_value_b type=complete

    int temp_result[T_2];
#pragma HLS array_partition variable=temp_result type=complete dim=1
#pragma HLS array_partition variable=list_a type=complete dim=1
#pragma HLS array_partition variable=list_a type=complete dim=2
#pragma HLS array_partition variable=list_b type=complete dim=1
#pragma HLS array_partition variable=list_b type=complete dim=2

    process_list: for (int j = 0; j < T_2; j++) {
#pragma HLS unroll
        int list_a_offset = offset_value_a.data[j];
        int list_a_len = length_value_a.data[j];
        int list_b_offset = offset_value_b.data[j];
        int list_b_len = length_value_b.data[j];
        int tc_num[1] = {0};
        setIntersection(list_a[j], list_b[j], list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);
        temp_result[j] = tc_num[0];
    }

    process_result_sum: for (int j = 0; j < T_2; j++) {
        tri_count += temp_result[j];
    }

    tc_count[0] = tri_count;
}


extern "C" {
void TriangleCount (int512* edge_list, int* offset_list_1, int* offset_list_2, \
                    int512* column_list_1, int512* column_list_2, int edge_num, int* tc_number ) {

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

    static hls::stream<int512> edgeStrm;
#pragma HLS STREAM variable = edgeStrm depth=16
    static hls::stream<int256> offsetStrm_A;
#pragma HLS STREAM variable = offsetStrm_A depth=16
    static hls::stream<int256> offsetStrm_B;
#pragma HLS STREAM variable = offsetStrm_B depth=16
    static hls::stream<int256> lengthStrm_A;
#pragma HLS STREAM variable = lengthStrm_A depth=16
    static hls::stream<int256> lengthStrm_B;
#pragma HLS STREAM variable = lengthStrm_B depth=16
    static hls::stream<int512> edgeStrmOut;
#pragma HLS STREAM variable = edgeStrmOut depth=16

    int list_a_ping[T_2][T][BUF_DEPTH];
    int list_b_ping[T_2][T][BUF_DEPTH];
    int list_a_pong[T_2][T][BUF_DEPTH];
    int list_b_pong[T_2][T][BUF_DEPTH];
#pragma HLS array_partition variable=list_a_ping type=complete dim=1
#pragma HLS array_partition variable=list_a_ping type=complete dim=2
#pragma HLS array_partition variable=list_b_ping type=complete dim=1 
#pragma HLS array_partition variable=list_b_ping type=complete dim=2
#pragma HLS array_partition variable=list_a_pong type=complete dim=1
#pragma HLS array_partition variable=list_a_pong type=complete dim=2
#pragma HLS array_partition variable=list_b_pong type=complete dim=1 
#pragma HLS array_partition variable=list_b_pong type=complete dim=2

    int256 offset_a_ping[1];
    int256 offset_a_pong[1];
    int256 offset_b_ping[1];
    int256 offset_b_pong[1];
#pragma HLS array_partition variable=offset_a_ping type=complete dim=1 
#pragma HLS array_partition variable=offset_a_pong type=complete dim=1 
#pragma HLS array_partition variable=offset_b_ping type=complete dim=1 
#pragma HLS array_partition variable=offset_b_pong type=complete dim=1

    int256 length_a_ping[1];
    int256 length_a_pong[1];
    int256 length_b_ping[1];
    int256 length_b_pong[1];
#pragma HLS array_partition variable=length_a_ping type=complete dim=1 
#pragma HLS array_partition variable=length_a_pong type=complete dim=1 
#pragma HLS array_partition variable=length_b_ping type=complete dim=1 
#pragma HLS array_partition variable=length_b_pong type=complete dim=1

    for (int t = 0; t < T_2; t++) { // initialize
#pragma HLS unroll
        offset_a_ping[0].data[t] = 0;
        offset_a_pong[0].data[t] = 0;
        offset_b_ping[0].data[t] = 0;
        offset_b_pong[0].data[t] = 0;
        length_a_ping[0].data[t] = 0;
        length_a_pong[0].data[t] = 0;
        length_b_ping[0].data[t] = 0;
        length_b_pong[0].data[t] = 0;
    }

    int list_a_cache[1][T][BUF_DEPTH];
#pragma HLS array_partition variable=list_a_cache type=complete dim=1
#pragma HLS array_partition variable=list_a_cache type=complete dim=2

    int list_a_tag[2];
#pragma HLS array_partition variable=list_a_tag type=complete dim=1

    list_a_tag[0] = -1; // invalid data in cache
    list_a_tag[1] = 0; // valid data length

    int triCount_ping[1]={0};
    int triCount_pong[1]={0};
    int length = edge_num*2;

    loadEdgeList(length, edge_list, edgeStrm);
    loadOffset(length, offset_list_1, offset_list_2, edgeStrm, edgeStrmOut, offsetStrm_A, offsetStrm_B, lengthStrm_A, lengthStrm_B);

    int loop = (length + T - 1) /T;
    int pp = 0; // ping-pong operation
    int TC_ping = 0;
    int TC_pong = 0;
    pp_load_cpy_process: for (int i = 0; i <= loop; i++) {
        int512 edge_strm_value = edgeStrmOut.read();
        int256 offset_strm_a = offsetStrm_A.read();
        int256 offset_strm_b = offsetStrm_B.read();
        int256 length_strm_a = lengthStrm_A.read();
        int256 length_strm_b = lengthStrm_B.read();

        if (pp) {
            processList (list_a_ping, list_b_ping, offset_a_ping, offset_b_ping, length_a_ping, length_b_ping, triCount_pong);
            loadCpyListA (edge_strm_value, column_list_1, offset_strm_a, length_strm_a, list_a_cache, list_a_tag, list_a_pong, offset_a_pong, length_a_pong);
            loadListB (offset_strm_b, length_strm_b, column_list_2, list_b_pong, offset_b_pong, length_b_pong);
            TC_pong += triCount_pong[0];
        } else {
            processList (list_a_pong, list_b_pong, offset_a_pong, offset_b_pong, length_a_pong, length_b_pong, triCount_ping);
            loadCpyListA (edge_strm_value, column_list_1, offset_strm_a, length_strm_a, list_a_cache, list_a_tag, list_a_ping, offset_a_ping, length_a_ping);
            loadListB (offset_strm_b, length_strm_b, column_list_2, list_b_ping, offset_b_ping, length_b_ping);
            TC_ping += triCount_ping[0];
        }
        pp = 1 - pp;
    }
    tc_number[0] = TC_ping + TC_pong;
}
}
