#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>


typedef struct v_datatype {int data[16];} int512;
#define BUF_DEPTH   4096
#define T           16
#define BURST_LEN   8
const int c_size = BUF_DEPTH;
// use 512 bit width to full utilize bandwidth


// need to add test txt file for debug.
void adjListCpy(int512* dst, int512* start_location, int offset, int len){
    int offset_begin = offset / T;
    int offset_end = (offset + len + T - 1) / T;
    int loop_num = (offset_end - offset_begin + BURST_LEN - 1) / BURST_LEN;

    for (int i = 0; i < loop_num; i++) {
        for (int j = 0; j < BURST_LEN; j++) {
#pragma HLS pipeline II=1
            if ((i*BURST_LEN + j) < (offset_end - offset_begin)) {
                for (int k = 0; k < T; k++) {
#pragma HLS unroll
                    dst[i*BURST_LEN + j].data[k] = start_location[i*BURST_LEN + j + offset_begin].data[k];
                }
            }
        }
    }
}

void setIntersection(int512 *list_a, int512 *list_b, int len_a, int len_b, int offset_a, int offset_b, int* tc_cnt)
{
    #pragma HLS inline off
    int count = 0;
    int idx_a = 0, idx_b = 0;
    int initial_512_a = offset_a & 0xf;
    int initial_512_b = offset_b & 0xf;
    while ((idx_a < len_a) && (idx_b < len_b))
    {
#pragma HLS pipeline ii=1

        int a_512_t = initial_512_a + idx_a;
        int a_512_block = a_512_t >> 4;
        int a_512_bias = a_512_t & 0xf;
        int value_a = list_a[a_512_block].data[a_512_bias];

        int b_512_t = initial_512_b + idx_b;
        int b_512_block = b_512_t >> 4;
        int b_512_bias = b_512_t & 0xf;
        int value_b = list_b[b_512_block].data[b_512_bias];

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

void loadIndexbuf(int* a_index_buf, int* b_index_buf, int* offset_list_1, int* offset_list_2, int* edge_list_buf, int process_len){
    for (int l = 0; l < 16; l++){
#pragma HLS pipeline ii=1
        if(l < process_len){
            a_index_buf[l*2] = offset_list_1[edge_list_buf[l*2]];
            a_index_buf[l*2+1] = offset_list_1[edge_list_buf[l*2] + 1];
            b_index_buf[l*2] = offset_list_2[edge_list_buf[l*2+1]];
            b_index_buf[l*2+1] = offset_list_2[edge_list_buf[l*2+1] + 1];
        }
    }
}

void updateLenbuf(int* len_a_buf, int* len_b_buf, int* a_index_buf, int* b_index_buf){
    for (int idx=0;idx<16; idx++){
        len_a_buf[idx] = a_index_buf[idx*2+1] - a_index_buf[idx*2];
        len_b_buf[idx] = b_index_buf[idx*2+1] - b_index_buf[idx*2];
    }
}

template <typename DT>
void loadEdgelist(int length, DT* inArr, hls::stream<DT>& inStrm) {
    for (int i = 0; i < length; i++) {
#pragma HLS loop_tripcount min = c_size max = c_size
// #pragma HLS pipeline ii = 1
        inStrm << inArr[i];
        /*
        if (i%2==0){
            std::cout<<"edgelist: "<< inArr[i] << "  ";
        } else{
            std::cout << inArr[i] << std::endl;
        }
        */
    }
    std::cout << "load " << length << "edges" << std::endl;
}

template <typename DT>
void loadOffset(int* offset_list_1, int* offset_list_2, int length,
                hls::stream<DT>& edgestrm, hls::stream<DT>& a_idx_strm, hls::stream<DT>& b_idx_strm, 
                hls::stream<DT>& len_a_strm, hls::stream<DT>& len_b_strm){
    for(int i=0; i< length; i++){
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
        // std::cout << "edgelist: " << a_offset << "  " << b_offset << std::endl;
        // std::cout << "offset & len: " << a_idx << " " << b_idx <<" "<< len_a<<" "<<len_b << std::endl;
    }
    std::cout << "load " << length << "offsets" << std::endl;
}

template <typename DT>
void loadAdjlist(int512* column_list_1, int512* column_list_2, int length,
                 hls::stream<DT>& a_idx_strm, hls::stream<DT>& b_idx_strm, 
                 hls::stream<DT>& len_a_strm, hls::stream<DT>& len_b_strm,
                 hls::stream<DT>& len_a_strm_o, hls::stream<DT>& len_b_strm_o,
                 hls::stream<DT>& a_idx_strm_o, hls::stream<DT>& b_idx_strm_o,
                 int512* list_a, int512* list_b){
    
    for(int i = 0; i<length; i++){
        int list_a_offset = a_idx_strm.read();
        int list_a_len = len_a_strm.read();
        int list_b_offset = b_idx_strm.read();
        int list_b_len = len_b_strm.read();

        adjListCpy(list_a, column_list_1, list_a_offset, list_a_len);
        adjListCpy(list_b, column_list_2, list_b_offset, list_b_len);

        len_a_strm_o << list_a_len;
        len_b_strm_o << list_b_len;
        a_idx_strm_o << list_a_offset;
        b_idx_strm_o << list_b_offset;
    }
    std::cout << "load " << length << "pair of adj lists" << std::endl;
}

template <typename DT>
void setInterStrm(int512* list_a, int512* list_b, int length,
                hls::stream<DT>& a_idx_strm, hls::stream<DT>& b_idx_strm,
                hls::stream<DT>& len_a_strm, hls::stream<DT>& len_b_strm,
                int* count){
    int temp_count[1] = {0};
    int triangle_count = 0;
    for(int i = 0; i<length; i++){
        int offset_a = a_idx_strm.read();
        int offset_b = b_idx_strm.read();
        int len_a = len_a_strm.read();
        int len_b = len_b_strm.read();
        setIntersection(list_a, list_b, len_a, len_b, offset_a, offset_b, temp_count);
        triangle_count += temp_count[0];
    }
    count[0] = triangle_count;
}


extern "C" {
void TriangleCount(int* edge_list, int* offset_list_1, int* offset_list_2, int512* column_list_1, int512* column_list_2, int edge_num, int* tc_number ) {

#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem0 port = edge_list
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem1 port = offset_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem2 port = offset_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem3 port = column_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    64 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem4 port = column_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    16 max_write_burst_length = 2 max_read_burst_length = 2  bundle = gmem5 port = tc_number

#pragma HLS INTERFACE s_axilite port = edge_list bundle = control
#pragma HLS INTERFACE s_axilite port = offset_list_1 bundle = control
#pragma HLS INTERFACE s_axilite port = offset_list_2 bundle = control
#pragma HLS INTERFACE s_axilite port = column_list_1 bundle = control
#pragma HLS INTERFACE s_axilite port = column_list_2 bundle = control
#pragma HLS INTERFACE s_axilite port = edge_num bundle = control
#pragma HLS INTERFACE s_axilite port = tc_number bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

// Initial local memory space of list_a and list_b
    int512 list_a[BUF_DEPTH];
    int512 list_b[BUF_DEPTH];

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
#pragma HLS STREAM variable = len_a_strm depth=16
#pragma HLS STREAM variable = len_b_strm depth=16

    int triCount[1]={0};

    loadEdgelist<int>(edge_num_local, edge_list, edgeInStrm);
    loadOffset<int>(offset_list_1, offset_list_2, edge_num, edgeInStrm, a_idx_strm, b_idx_strm, len_a_strm, len_b_strm);
    loadAdjlist<int>(column_list_1, column_list_2, edge_num, a_idx_strm, b_idx_strm, len_a_strm, len_b_strm, 
                len_a_strm_o, len_b_strm_o, a_idx_strm_o, b_idx_strm_o, list_a, list_b);
    setInterStrm<int>(list_a, list_b, edge_num, a_idx_strm_o, b_idx_strm_o, len_a_strm_o, len_b_strm_o, triCount);

    tc_number[0] = triCount[0];

}
}