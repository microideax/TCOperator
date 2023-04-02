// tc_refactor file
#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>

// use 512 bit width to full utilize bandwidth
typedef struct data_type {int data[16];} int512;
#define BUF_DEPTH   512
#define T           16
#define T_offset    4
#define T_2         8

void loadEdgeList(int length, int512* inArr, hls::stream<int512>& edgeStrm) {
    int loop = (length + T - 1) / T;
    for (int i = 0; i < loop; i++) {
#pragma HLS pipeline ii = 1
        edgeStrm << inArr[i];
    }
}

void loadOffset(int length, int* offset_list_1, int* offset_list_2, hls::stream<int512>& edgeStrm,
                hls::stream<int512>& offsetStrm, hls::stream<int512>& lengthStrm, hls::stream<int512>& edgeStrmOut) {
    int loop = (length + T - 1) / T;
    for (int i = 0; i < loop; i++) {
        int512 edge_value = edgeStrm.read();
#pragma HLS array_partition variable=edge_value type=complete dim=1
        int512 length_value;
        int512 offset_value;
#pragma HLS array_partition variable=length_value type=complete dim=1
#pragma HLS array_partition variable=offset_value type=complete dim=1
        for (int j = 0; j < T_2; j++) {
#pragma HLS pipeline
            if (i*T + 2*j < length) {
                int a_value = edge_value.data[j*2];
                int a_offset = offset_list_1[a_value];
                int a_length = offset_list_1[a_value + 1] - a_offset;
                length_value.data[2*j] = a_length;
                offset_value.data[2*j] = a_offset;
                int b_value = edge_value.data[j*2 + 1];
                int b_offset = offset_list_2[b_value];
                int b_length = offset_list_2[b_value + 1] - b_offset;
                length_value.data[2*j + 1] = b_length;
                offset_value.data[2*j + 1] = b_offset;
            }
            else {  
                length_value.data[2*j] = 0;
                offset_value.data[2*j] = 0;
                length_value.data[2*j + 1] = 0;
                offset_value.data[2*j + 1] = 0;
                edge_value.data[2*j] = -2;
                edge_value.data[2*j + 1] = -2;
            }
        }
        offsetStrm << offset_value;
        lengthStrm << length_value;
        edgeStrmOut << edge_value;
    }
}

void loadCpyListA ( int512 edge_value, int512 offset_value, int512 length_value, int512* column_list_1, \
                    int list_a_cache[1][T][BUF_DEPTH], int list_a_tag[2], int list_a[T_2][T][BUF_DEPTH]) {
#pragma HLS inline
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
            int o_begin_a = offset_value.data[2*j] >> T_offset;
            int o_end_a = (offset_value.data[2*j] + length_value.data[2*j] + T - 1) >> T_offset;
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
}

void loadListB (int512 offset_value, int512 length_value, int512* column_list_2, int list_b[T_2][T][BUF_DEPTH]) {
#pragma HLS inline
    // load list_b
    load_list_b_T_2: for (int j = 0; j < T_2; j++) {
        // TODO: add coalesce
        int o_begin_b = offset_value.data[2*j + 1] >> T_offset;
        int o_end_b = (offset_value.data[2*j + 1] + length_value.data[2*j + 1] + T - 1) >> T_offset;
        load_list_b: for (int ii = o_begin_b; ii < o_end_b; ii++) {
#pragma HLS pipeline
            int512 list_b_temp = column_list_2[ii];
            for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                list_b[j][jj][ii - o_begin_b] = list_b_temp.data[jj];
            }
        }
    }

}

void loadAdjList(hls::stream<int512>& offsetStrm, hls::stream<int512>& lengthStrm, hls::stream<int512>& edgeStrmIn, \
                 int512* column_list_1, int512* column_list_2, int list_a_cache[1][T][BUF_DEPTH], int list_a_tag[2], \
                 hls::stream<int512>& offsetStrmOut, hls::stream<int512>& lengthStrmOut, \
                 int list_a[T_2][T][BUF_DEPTH], int list_b[T_2][T][BUF_DEPTH]) {
#pragma HLS inline
    // There exists different strategies to process list_a load and list_b load;
    // For list_a load: HitMissCheck -> load/copy list -> update cache;
    // For list_b load: coalesce request.(TODO)

    int512 edge_value = edgeStrmIn.read();
    int512 offset_value = offsetStrm.read();
    int512 length_value = lengthStrm.read();

    offsetStrmOut << offset_value;
    lengthStrmOut << length_value;

    int512 offset_value_b = offset_value;
    int512 length_value_b = length_value;

#pragma dataflow
    // this dataflow pragma does not work... :-( , seem HLS complier only optimizes top-level dataflow pragma.
    loadCpyListA(edge_value, offset_value, length_value, column_list_1, list_a_cache, list_a_tag, list_a);
    loadListB(offset_value_b, length_value_b, column_list_2, list_b);
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
loop_set_inetrsection: while ((idx_a < list_a_len + o_idx_a) && (idx_b < list_b_len + o_idx_b))
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

void processList(int list_a[T_2][T][BUF_DEPTH], int list_b[T_2][T][BUF_DEPTH], \
                 hls::stream<int512>& offsetStrmIn, hls::stream<int512>& lengthStrmIn, int* tc_count) {
#pragma HLS inline
    	int tri_count = 0;

    int512 offset_value = offsetStrmIn.read();
    int512 length_value = lengthStrmIn.read();
#pragma HLS array_partition variable=offset_value type=complete
#pragma HLS array_partition variable=length_value type=complete

    int temp_result[T_2];
#pragma HLS array_partition variable=temp_result type=complete dim=1
#pragma HLS array_partition variable=list_a type=complete dim=1
#pragma HLS array_partition variable=list_a type=complete dim=2
#pragma HLS array_partition variable=list_b type=complete dim=1
#pragma HLS array_partition variable=list_b type=complete dim=2

    process_list: for (int j = 0; j < T_2; j++) {
#pragma HLS unroll
        int list_a_offset = offset_value.data[2*j];
        int list_a_len = length_value.data[2*j];
        int list_b_offset = offset_value.data[2*j + 1];
        int list_b_len = length_value.data[2*j + 1];
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
    static hls::stream<int512> offsetStrm;
#pragma HLS STREAM variable = offsetStrm depth=16
    static hls::stream<int512> lengthStrm;
#pragma HLS STREAM variable = lengthStrm depth=16
    static hls::stream<int512> edgeStrmOut;
#pragma HLS STREAM variable = edgeStrmOut depth=16
    static hls::stream<int512> offsetStrmOut;
#pragma HLS STREAM variable = offsetStrmOut depth=16
    static hls::stream<int512> lengthStrmOut;
#pragma HLS STREAM variable = lengthStrmOut depth=16


    int list_a[T_2][T][BUF_DEPTH];
    int list_b[T_2][T][BUF_DEPTH];
    int list_a_cache[1][T][BUF_DEPTH];
#pragma HLS array_partition variable=list_a type=complete dim=1
#pragma HLS array_partition variable=list_a type=complete dim=2
#pragma HLS array_partition variable=list_b type=complete dim=1 
#pragma HLS array_partition variable=list_b type=complete dim=2
#pragma HLS array_partition variable=list_a_cache type=complete dim=1
#pragma HLS array_partition variable=list_a_cache type=complete dim=2
    int list_a_tag[2];
#pragma HLS array_partition variable=list_a_tag type=complete dim=1
    list_a_tag[0] = -1; // invalid data in cache
    list_a_tag[1] = 0; // valid data length
    int triCount[1]={0};
    int length = edge_num*2;

    loadEdgeList(length, edge_list, edgeStrm);
    loadOffset(length, offset_list_1, offset_list_2, edgeStrm, offsetStrm, lengthStrm, edgeStrmOut);

    int loop = (length + T - 1) / T;
    int Tri_num = 0;
    for (int i = 0; i < loop; i++) {
        loadAdjList(offsetStrm, lengthStrm, edgeStrmOut, column_list_1, column_list_2, \
                    list_a_cache, list_a_tag, offsetStrmOut, lengthStrmOut, list_a, list_b);
        processList(list_a, list_b, offsetStrmOut, lengthStrmOut, triCount);
        Tri_num += triCount[0];
    }

    tc_number[0] = Tri_num;
}
}
