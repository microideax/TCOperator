#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>


typedef struct v_datatype {int data[16];} int512;
#define BUF_DEPTH   512
#define T           16
#define T_offset    4
#define BURST_LEN   8
#define MAX_colase  64
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

template <typename DT>
void procIntersec (int512* column_list_1, int512* column_list_2, int length, int* tri_count,
                    hls::stream<DT>& a_colase_idx_strm, hls::stream<DT>& a_colase_len_strm, \
                    hls::stream<DT>& b_colase_idx_strm, hls::stream<DT>& b_colase_len_strm, \
                    hls::stream<bool>& a_colase_strm, hls::stream<bool>& b_colase_strm, \
                    hls::stream<bool>& end_stream) {
    
    // int temp_count[1] = {0};
    int triangle_count = 0;
    int list_a [BUF_DEPTH][T];
    int list_b [BUF_DEPTH][T];
#pragma HLS array_partition variable=list_a type=complete dim=2
#pragma HLS array_partition variable=list_b type=complete dim=2

    int list_a_len;
    int list_a_offset;
    int list_b_len;
    int list_b_offset;

    bool end_flag = end_stream.read();
    while (!end_flag) {
        bool a_colase = a_colase_strm.read();
        bool b_colase = b_colase_strm.read();

        if (a_colase && b_colase) {
#pragma HLS dataflow
            // adj list cpy.
            list_a_len = a_colase_len_strm.read();
            list_a_offset = a_colase_idx_strm.read();
            list_b_len = b_colase_len_strm.read();
            list_b_offset = b_colase_idx_strm.read();

            int o_begin_a = list_a_offset >> T_offset;
            int o_end_a = (list_a_offset + list_a_len + T - 1) >> T_offset;
            int o_begin_b = list_b_offset >> T_offset;
            int o_end_b = (list_b_offset + list_b_len + T - 1) >> T_offset;

            loop_a_adj_cpy: for (int i = o_begin_a; i < o_end_a; i++) {
#pragma HLS pipeline
                int512 temp_a = column_list_1[i];
                for (int j = 0; j < T; j++) {
#pragma HLS unroll
                    list_a[i - o_begin_a][j] = temp_a.data[j];
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
        }

        if ((!a_colase) && (!b_colase)) {

            int list_b_len_t = b_colase_len_strm.read();
            int list_b_offset_t = b_colase_idx_strm.read();

            int count = 0;
            int o_idx_a = list_a_offset & 0xf;
            int o_idx_b = (list_b_offset & 0xf) + (list_b_offset_t - list_b_offset);
            int idx_a = o_idx_a;
            int idx_b = o_idx_b;
            loop_set_inetrsection: while ((idx_a < list_a_len + o_idx_a) && (idx_b < list_b_len_t + o_idx_b))
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
