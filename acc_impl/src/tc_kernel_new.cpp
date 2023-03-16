#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>


typedef struct v_datatype {int data[16];} int512;
#define BUF_DEPTH   512
#define T           16
#define T_offset    4
#define BURST_LEN   8
const int c_size = BUF_DEPTH;
// use 512 bit width to full utilize bandwidth


// need to add test txt file for debug.
void adjListCpy(int* dst, int512* start_location, int offset, int len){
    
    int offset_begin = offset / T;
    int offset_end = (offset + len + T - 1) / T;
    int offset_idx = offset & 0xf;
    for (int i = offset_begin; i < offset_end; i++) {
#pragma HLS pipeline
        int512 temp = start_location[i];
        for (int j = 0; j < T; j++) {
#pragma HLS unroll
            int t = (i - offset_begin) * T + j - offset_idx;
            if (t >= 0)
                dst[t] = temp.data[j];
        }
    }
}

void setIntersection(int* list_a, int* list_b, int len_a, int len_b, int* tc_cnt)
{
#pragma HLS inline off
    int count = 0;
    int idx_a = 0, idx_b = 0;
    while ((idx_a < len_a) && (idx_b < len_b))
    {
#pragma HLS pipeline ii=1

        int value_a = list_a[idx_a];
        int value_b = list_b[idx_b];

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
    tc_cnt[0] = count;
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
void loadOffset(int* offset_list_1, int* offset_list_2, int length,
                hls::stream<DT>& edgestrm, hls::stream<DT>& a_idx_strm, hls::stream<DT>& b_idx_strm, 
                hls::stream<DT>& len_a_strm, hls::stream<DT>& len_b_strm){
    for(int i = 0; i < length; i++) {
        int a_offset = edgestrm.read();
        int b_offset = edgestrm.read();
        int a_idx = offset_list_1[a_offset];
        int b_idx = offset_list_2[b_offset];
        int len_a = offset_list_1[a_offset +1] - a_idx;
        int len_b = offset_list_2[b_offset +1] - b_idx;
        a_idx_strm << a_idx;
        b_idx_strm << b_idx;
        len_a_strm << len_a;
        len_b_strm << len_b;
    }
}

template <typename DT>
void procIntersec(int512* column_list_1, int512* column_list_2, int length,
                 hls::stream<DT>& a_idx_strm, hls::stream<DT>& b_idx_strm, 
                 hls::stream<DT>& len_a_strm, hls::stream<DT>& len_b_strm,
                 int* count){
    
    // int temp_count[1] = {0};
    int triangle_count = 0;
    int list_a [BUF_DEPTH][T];
    int list_b [BUF_DEPTH][T];
#pragma HLS array_partition variable=list_a type=complete dim=2
#pragma HLS array_partition variable=list_b type=complete dim=2

    int previous_a_offset = -1;

    for(int i = 0; i < length; i++) {

        int list_a_offset = a_idx_strm.read();
        int list_a_len = len_a_strm.read();
        int list_b_offset = b_idx_strm.read();
        int list_b_len = len_b_strm.read();

        // adjListCpy(list_a, column_list_1, list_a_offset, list_a_len);
        // adjListCpy(list_b, column_list_2, list_b_offset, list_b_len);

        int o_begin_a = list_a_offset >> T_offset;
        int o_end_a = (list_a_offset + list_a_len + T - 1) >> T_offset;
        int o_begin_b = list_b_offset >> T_offset;
        int o_end_b = (list_b_offset + list_b_len + T - 1) >> T_offset;

        if (previous_a_offset != list_a_offset) {
            loop_a_adj_cpy: for (int i = o_begin_a; i < o_end_a; i++) {
    #pragma HLS pipeline
                int512 temp_a = column_list_1[i];
                for (int j = 0; j < T; j++) {
    #pragma HLS unroll
                    list_a[i - o_begin_a][j] = temp_a.data[j];
                }
            }
        }


        loop_b_adj_cpy: for (int i = o_begin_b; i < o_end_b; i++) {
#pragma HLS pipeline
            int512 temp_b = column_list_2[i];
            for (int j = 0; j < T; j++) {
#pragma HLS unroll
                list_b[i - o_begin_b][j] = temp_b.data[j];
            }
        }

        // setIntersection(list_a, list_b, list_a_len, list_b_len, temp_count);
        int count = 0;
        int o_idx_a = list_a_offset & 0xf;
        int o_idx_b = list_b_offset & 0xf;
        int idx_a = o_idx_a, idx_b = o_idx_b;
        loop_set_inetrsection: while ((idx_a < list_a_len + o_idx_a) && (idx_b < list_b_len + o_idx_b))
        {
#pragma HLS pipeline ii=1
            int a_dim_1 = idx_a >> T_offset;
            int a_dim_2 = idx_a & 0xf;

            int b_dim_1 = idx_b >> T_offset;
            int b_dim_2 = idx_b & 0xf;

            int value_a = list_a[a_dim_1][a_dim_2];
            int value_b = list_b[b_dim_1][b_dim_2];

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
        triangle_count += count;
        previous_a_offset = list_a_offset;
    }
    count[0] = triangle_count;
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
#pragma HLS STREAM variable = edgeInStrm depth=32
    static hls::stream<int> a_idx_strm, b_idx_strm;
#pragma HLS STREAM variable = a_idx_strm depth=16
#pragma HLS STREAM variable = b_idx_strm depth=16
    static hls::stream<int> a_idx_strm_o, b_idx_strm_o;
#pragma HLS STREAM variable = a_idx_strm_o depth=16
#pragma HLS STREAM variable = b_idx_strm_o depth=16
    static hls::stream<int> len_a_strm, len_b_strm;
#pragma HLS STREAM variable = len_a_strm depth=16
#pragma HLS STREAM variable = len_b_strm depth=16
    static hls::stream<int> len_a_strm_o, len_b_strm_o;
#pragma HLS STREAM variable = len_a_strm_o depth=16
#pragma HLS STREAM variable = len_b_strm_o depth=16

    int triCount[1]={0};

    loadEdgelist<int>(edge_num_local, edge_list, edgeInStrm);
    loadOffset<int>(offset_list_1, offset_list_2, edge_num, edgeInStrm, a_idx_strm, b_idx_strm, len_a_strm, len_b_strm);
    procIntersec<int>(column_list_1, column_list_2, edge_num, a_idx_strm, b_idx_strm, len_a_strm, len_b_strm, triCount);

    tc_number[0] = triCount[0];

}
}
