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

template <typename DT>
void burstReadStrm(int length, DT* inArr, hls::stream<DT>& inStrm) {
    for (int i = 0; i < length; i++) {
#pragma HLS loop_tripcount min = c_size max = c_size
// #pragma HLS pipeline ii = 1
        inStrm << inArr[i];
    }
}


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

template <typename DT>
int binarySearch(DT arr[], int low, int high, DT key){
    int mid = 0;
    bsearch: while (low <= high){
        mid = low + ((high -low)>>1);
        if(arr[mid] == key){
            return mid;
        } else if (arr[mid] < key){
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return -1;
}

int highestPowerof2(int n) {
    int res = 0;
    if(n > 0) {
        res = 1;
        while ((res<<1) <= n) {
            res <<= 1;
        }
    }
    return res;
}

int msb(unsigned int v) {
	static const int pos[32] = { 0, 1, 28, 
				     2, 29, 14, 24, 
				     3, 30, 22, 20, 15, 25, 17,
				     4, 8,  31, 27, 13, 23, 21, 19, 16,
				     7, 26, 12, 18, 
				     6, 11,
				     5, 10, 9};
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v = (v >> 1) + 1;
	return pos[(v* 0x077CB531) >> 27];
}

int lbitBinarySearch(int512 arr[], int low, int high, int offset, int key){
    // int k = highestPowerof2(high);
    int init = offset & 0xf;
    int lbit = msb(high);

    int k = high ? 1 << lbit : 0;
    int idx_k = k + init;
    int block_k = idx_k >> 4;
    int bias_k = idx_k & 0xf;
    int temp_value = arr[block_k].data[bias_k];

    int r, idx_r, block_r, bias_r;
    int i = (temp_value <= key) ? k : 0;
    int idx_i, block_i, bias_i;
    while ((k >>= 1)){
#pragma HLS pipeline ii=1
        r = i + k;
        idx_r = r + init;
        block_r = idx_r >> 4;
        bias_r = idx_r & 0xf;
        temp_value = arr[block_r].data[bias_r];

        if ((r <= high) && (key > temp_value)) {
            i = r;
        } else if ((r <= high) && (key == temp_value)) {
            return r;
        }
    }

    idx_i = i + init;
    block_i = idx_i >> 4;
    bias_i = idx_i & 0xf;
    temp_value = arr[block_i].data[bias_i];
    if (temp_value == key) {
        return i;
    } else {
        return -1;
    }
}

void setInterBinarySearch(int512 *list_a, int512 *list_b, int len_a, int len_b, int offset_a, int offset_b, int* tc_cnt){
/** Need to make sure the list_a is the shorter one **/
    unsigned int count = 0;
    int temp_idx_0 = 0;
    // list_a has to be the shorter one, search with element from shorter array
    setint: for (int i = 0; i < len_a; i++){
        int ini_idx_a = offset_a & 0xf;
        int idx = ini_idx_a + i;
        int block_a = idx >> 4;
        int bias_a = idx & 0xf;
        temp_idx_0 = lbitBinarySearch(list_b, 0, len_b-1, offset_b, list_a[block_a].data[bias_a]);
        // temp_idx_0 = binarySearch<DT>(list_b, 0, len_b-1, list_a[i]);
        if (temp_idx_0 != -1) {
            count++;
        }
    }
    tc_cnt[0] = count;
}

template <typename DT>
void setInterBinarySearchMT(DT *list_a, DT *list_b, int len_a, int len_b, int* tc_cnt){
/** Need to make sure the list_a is the shorter one **/
    unsigned int count = 0;
    int temp_idx_0 = 0;
    // list_a has to be the shorter one, search with element from shorter array
    setintmt: for (int i = 0; i < len_a; i++){
#pragma HLS unroll factor = 4
        temp_idx_0 = binarySearch<DT>(list_b, 0, len_b-1, list_a[i]);
        if (temp_idx_0 != -1) {
            count++;
        }
    }
    tc_cnt[0] = count;
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

void loadEdgelist(int* edge_list, int* edge_list_buf, int offset, int loop_boundary){
#pragma HLS inline
    for (int j = 0; j < 32; j++){
#pragma HLS pipeline ii=1
        if(j < loop_boundary){
            edge_list_buf[j] = edge_list[offset*32 + j];
        } else {
            edge_list_buf[j] = -1;
        }
    }
}

void loadIndexbuf(int* a_index_buf, int* b_index_buf, int* offset_list_1, int* offset_list_2, int* edge_list_buf, int process_len){
#pragma HLS inline
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
#pragma HLS inline
    for (int idx=0;idx<16; idx++){
        len_a_buf[idx] = a_index_buf[idx*2+1] - a_index_buf[idx*2];
        len_b_buf[idx] = b_index_buf[idx*2+1] - b_index_buf[idx*2];
    }
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
// #pragma HLS RESOURCE variable=list_a core=XPM_MEMORY uram
// #pragma HLS RESOURCE variable=list_b core=XPM_MEMORY uram
    int edge_list_buf[32]; // buffer 16 <V1, V2> pairs
    int a_index_buf[32];
    int b_index_buf[32];
    int len_a_buf[16];
    int len_b_buf[16];
    // int node_b_buf[16];

    int triangle_count = 0;

    int main_loop_itr = edge_num*2/32 + 1;
    int edge_boundary = edge_num * 2;
    int loop_boundary = 32;
    int process_len = 16;
    int bs_counter = 0;

    static hls::stream<int> edgeInStrm("input_edge");
#pragma HLS STREAM variable = edgeInStrm depth=32

    // loadEdgelist(edge_list, edgeInStrm, 0, edge_num*2);
    // loadOffset(edgeInStrm, a_idx_strm, b_idx_strm, len_a_strm, len_b_strm, offset_list_1, offset_list_2);
    // loadAdjList(a_idx_strm, b_idx_strm, len_a_strm, len_b_strm, column_list_1, column_list_2, list_a, list_b);
    // interSection(list_a, list_b, len_a_strm, len_b_strm, triCount);

    // main_loop: for (int i = 0; i < edge_num*2/32+1; i++) {
    main_loop: for (int i = 0; i < main_loop_itr; i++) {
        //load edge_list to edgelist buffer
        // i%2==0, edge_list_buf_0; else edge_list_buf_1
// #pragma HLS pipeline rewind
        loop_boundary = edge_boundary - i*32;
        loadEdgelist(edge_list, edge_list_buf, i, loop_boundary);

        process_len = (i*16 < edge_num) ? 16 : (edge_num - i*16);
        loadIndexbuf(a_index_buf, b_index_buf, offset_list_1, offset_list_2, edge_list_buf, process_len);

        updateLenbuf(len_a_buf, len_b_buf, a_index_buf, b_index_buf);

        for (int k = 0; k < process_len; k++){
            int node_a = edge_list_buf[k*2];
            int node_b = edge_list_buf[k*2 + 1];
            int previous_node_a = -1;

            // int node_a = a_index_buf[k*2];
            // int node_b = b_index_buf[k*2];
            int vertex_a_idx = a_index_buf[k*2];
            int vertex_b_idx = b_index_buf[k*2];
            // std::cout<< "read nodes: "<< node_a <<", "<<node_b << std::endl;
            // int len_a = offset_list_1[node_a + 1] - vertex_a_idx;
            // int len_b = offset_list_2[node_b + 1] - vertex_b_idx;
            int len_a = len_a_buf[k];
            int len_b = len_b_buf[k];
            // std::cout<< "lens of lists: "<< len_a <<", "<< len_b << std::endl;
            
            int short_list = 0;
            int long_list = 0;

            if ((len_a != 0) && (len_b != 0) && (node_a != -1) && (node_b != -1)) {
                // burstCpyArray<int>(column_list_1, list_a, len_a, vertex_a_idx);
                // burstCpyArray<int>(column_list_2, list_b, len_b, vertex_b_idx);
                if (previous_node_a != node_a){
                    adjListCpy(list_a, column_list_1, vertex_a_idx, len_a);   
                    previous_node_a = node_a; 
                }
                adjListCpy(list_b, column_list_2, vertex_b_idx, len_b);

                // Process setintersection on lists with the lens are not zeros
                int temp_count[1] = {0};

                if(len_b / len_a >= 32){
                    setInterBinarySearch(list_a, list_b, len_a, len_b, vertex_a_idx, vertex_b_idx, temp_count);
                   bs_counter++;
                } else if (len_a / len_b > 32) {
                    setInterBinarySearch(list_b, list_a, len_b, len_a, vertex_b_idx, vertex_a_idx, temp_count);
                   bs_counter++;
                } 
		        else {
                   setIntersection(list_a, list_b, len_a, len_b, vertex_a_idx, vertex_b_idx, temp_count);
                }
                triangle_count += temp_count[0];
            }
        }
    }
    tc_number[0] = triangle_count;
    std::cout<<"Binary searched edge number: "<< bs_counter << std::endl;
}
}
