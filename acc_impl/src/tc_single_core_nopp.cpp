// refactor on 9-26 refactor
#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>
#include <algorithm>
#include "ap_int.h"

#define T               16
#define T_offset        4
#define E_per_burst     8
#define Parallel        8
/*cache in load list*/
#define CACHE_ENABLE    true
#define OFFSET_CACHE_SIZE   256
#define OFFSET_CACHE_OFFSET 8
#define OFFSET_CACHE_MASK   0xff
#define debug           false
#define Profile         true
#define L_LEN           32
/*cache in list b load*/
#define CACHE_B_LEN     512
#define CACHE_B_OFFSET  9
#define CACHE_B_MASK    0x1ff

typedef struct data_64bit_type {int data[2];} int64;
typedef struct data_512bit_type {int data[16];} int512; // for off-chip memory data access.
typedef struct data_custom_type {
    int offset[Parallel];
    int length[Parallel];
} para_int; // for parallel processing.
typedef struct data_cache_type {
    int tag;
    int offset;
    int len;
} cache_line_t; // for offset cache design.

int a_cacheHits = 0;
int a_cacheMisses = 0;  
int b_cacheHits = 0;                                                                                                        
int b_cacheMisses = 0;  

#if Profile==true
    int hit_count_list_a = 0;
    int miss_count_list_a = 0;
    int hit_count_list_b = 0;
    int miss_count_list_b = 0;
    int memory_access_a = 0;
    int memory_access_b = 0;
    int data_amount_off_chip_a = 0;
    int data_amount_off_chip_b = 0;
#endif


void loadEdgeList(int length, int512* inArr, hls::stream<int512>& eStrmOut) {
    int loop = (length + T - 1) / T;
    for (int i = 0; i < loop; i++) {
#pragma HLS pipeline ii = 1
        eStrmOut << inArr[i];
    }
}

void cacheLoad(cache_line_t* cache, int64 *offset_port,                                                                     
                 int nodeIndex, int& offset, int& len,                                                                       
                 int& hitCounter, int& missCounter) {
#pragma HLS inline
    int lineIndex = nodeIndex >> OFFSET_CACHE_OFFSET;
    int tag = nodeIndex;                                                                                                     
    int offset_1 = 0;                                                                                                       
    if (cache[lineIndex].tag == tag) {                                                                                      
        // Cache hit                                                                                                        
        len = cache[lineIndex].len;                                                                                         
        offset = cache[lineIndex].offset;                                                                                   
        hitCounter++;                                                                                                            
    } else {                                                                                                                
        // Cache miss 
        cache[lineIndex].tag = tag;
        int64 temp_value = offset_port[nodeIndex];
        offset = temp_value.data[0];
        offset_1 = temp_value.data[1];
        cache[lineIndex].offset = offset;
        len = offset_1 - offset;
        cache[lineIndex].len = len;
        missCounter++;
    }
}

void loadOffset(int length, int64* offset_list, hls::stream<int512>& eStrmIn, \
                hls::stream<para_int>& StrmA, hls::stream<para_int>& StrmB) {
    int loop = (length + T - 1) / T;

    para_int a_para;
    para_int b_para;
#pragma HLS DATA_PACK variable=a_para
#pragma HLS ARRAY_PARTITION variable=a_para.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=a_para.length complete dim=1
#pragma HLS DATA_PACK variable=b_para
#pragma HLS ARRAY_PARTITION variable=b_para.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_para.length complete dim=1

#if CACHE_ENABLE==true
    cache_line_t cache_a[OFFSET_CACHE_SIZE];
    cache_line_t cache_b[OFFSET_CACHE_SIZE];

// offset cache initialize
    for (int i = 0; i < OFFSET_CACHE_SIZE; i++) {
#pragma HLS pipeline ii = 1
        cache_a[i].tag = -1;
        cache_b[i].tag = -1;
    }
#endif

    for (int i = 0; i < loop; i++) {
        int512 edge_value = eStrmIn.read();
#pragma HLS array_partition variable=edge_value type=complete dim=1

        int a_value, a_offset, a_length;
        int b_value, b_offset, b_length;
        for (int j = 0; j < E_per_burst; j++) {
#pragma HLS pipeline // this pipeline has an II=3 and depth=28 because of the cacheLoad function
            if (i*T + 2*j < length) {
                a_value = edge_value.data[j*2];
                b_value = edge_value.data[j*2 + 1];
#if CACHE_ENABLE==true
                cacheLoad(cache_a, offset_list, a_value, a_offset, a_length, a_cacheHits, a_cacheMisses);
                cacheLoad(cache_b, offset_list, b_value, b_offset, b_length, b_cacheHits, b_cacheMisses);
#else
                a_offset = offset_list[a_value];
                a_length = offset_list[a_value + 1] - a_offset;
                b_offset = offset_list[b_value];
                b_length = offset_list[b_value + 1] - b_offset;
#endif
            } else {
                a_offset = 0;
                a_length = 0;
                b_offset = 0;
                b_length = 0;
            }

            a_para.offset[j] = a_offset;
            a_para.length[j] = a_length;
            b_para.offset[j] = b_offset;
            b_para.length[j] = b_length;
        // std::cout << "a_value, b_value: " << a_value << ", " << b_value << std::endl;
        // std::cout << "Loaded a_offset, b_offset: " << a_offset << ", " << b_offset << std::endl;
        // std::cout << "Loaded a_length, b_length: " << a_length << ", " << b_length << std::endl;
        }
        StrmA << a_para;
        StrmB << b_para;
    }
}

// listTaskSplitter function takes the streams from the loadOffset function and split them into small tasks for the following loadCpyListA and loadCpyListB functions
void elistTaskSplitter (int length, hls::stream<para_int>& StrmA, hls::stream<para_int>& StrmB,
                    // output split streams
                    hls::stream<bool>& ctrlStrmOut,
                    hls::stream<para_int>& StrmAOut,
                    hls::stream<para_int>& StrmBOut
                    ){
    int loop = (length + T - 1) / T;
    
    para_int a_para_i;
    para_int b_para_i;
    para_int a_para_o;
    para_int b_para_o;
    bool ctrl_para_o;

#pragma HLS DATA_PACK variable=a_para_o
#pragma HLS ARRAY_PARTITION variable=a_para_o.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=a_para_o.length complete dim=1
#pragma HLS DATA_PACK variable=b_para_o
#pragma HLS ARRAY_PARTITION variable=b_para_o.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_para_o.length complete dim=1

    int count = 0;
    
    for (int i = 0; i < loop; i++) {
        a_para_i = StrmA.read();
        b_para_i = StrmB.read();
        for (int j = 0; j < E_per_burst; j++) {
#pragma HLS pipeline
            int a_offset_i = a_para_i.offset[j];
            int a_length_i = a_para_i.length[j];
            int b_offset_i = b_para_i.offset[j];
            int b_length_i = b_para_i.length[j];
            if ((a_length_i > 0) && (b_length_i > 0)) { // filter len = 0;
                // for (int k = 0; k < (a_length_i + L_LEN -1)/L_LEN; k++) { // a split 
            // #pragma HLS pipeline
                    // int a_offset_tmp = a_offset_i + k*L_LEN;
                    // int a_length_tmp = ((a_length_i - k*L_LEN) < L_LEN) ? (a_length_i - k*L_LEN) : L_LEN;
                    // for (int l = 0; l < (b_length_i + L_LEN - 1)/L_LEN; l++) { // b split
                        a_para_o.offset[count] = a_offset_i;
                        a_para_o.length[count] = a_length_i;
                        // int b_offset_tmp = b_offset_i + l*L_LEN;
                        // int b_length_tmp = ((b_length_i - l*L_LEN) < L_LEN) ? (b_length_i - l*L_LEN) : L_LEN;
                        b_para_o.offset[count] = b_offset_i;
                        b_para_o.length[count] = b_length_i;

                        count ++;
                        if (count == Parallel) {
                            count = 0;
                            StrmAOut << a_para_o;
                            StrmBOut << b_para_o;
                            ctrlStrmOut << true; // not the end stream
                        }
                    // }
                // }
            }
        }
    }

    for (int i = count; i < Parallel; i++) {
        a_para_o.offset[i] = 0;
        a_para_o.length[i] = 0;
        b_para_o.offset[i] = 0;
        b_para_o.length[i] = 0;
    }
    StrmAOut << a_para_o;
    StrmBOut << b_para_o;
    ctrlStrmOut << true; // not the end stream
    StrmAOut << a_para_o;
    StrmBOut << b_para_o;
    ctrlStrmOut << false; // end stream
}

// loadCpyListA function contains :  
// <1> hit-miss check; 
// <2> load data from off-chip memory or cache;
// <3> update cache
// input:  column_list + a_offset + a_length + list_a_cache + list_a_cache_tag + list_a_tag;
// output: list_a + list_a_tag;
void loadCpyListA ( para_int a_element_in, int512* column_list, 
                    int list_a_cache[T], int list_a_cache_tag[1], 
                    int list_a[Parallel][T], int list_a_tag[Parallel],
                    para_int a_element_out[1]) {

    para_int a_value = a_element_in;
#pragma HLS DATA_PACK variable=a_value

    int list_cache_tag = list_a_cache_tag[0];
    int512 list_a_temp; // need array partition for unroll
#pragma HLS ARRAY_PARTITION variable=list_a_temp complete dim=1

    load_copy_list_a_Parallel: for (int j = 0; j < Parallel; j++) {
#pragma HLS pipeline

        int offset_a = a_value.offset[j];
        int length_a = a_value.length[j];

        if (offset_a == list_a_tag[j]) {
            // hit, no need to copy data
    #if Profile==true
            hit_count_list_a++;
    #endif

        } else if (offset_a == list_cache_tag) {
            // hit, copy data from list_a cache
    #if Profile==true
            hit_count_list_a++;
    #endif
            list_a_tag[j] = list_cache_tag;
            copy_list_a: for (int jj = 0; jj < T; jj++) { // just copy in a cycle
#pragma HLS unroll
                list_a[j][jj] = list_a_cache[jj];
            }
        } else {
            // miss, load data from off-chip memory and update cache

            int o_begin_a = offset_a >> T_offset;
            int o_end_a = (offset_a + length_a + T - 1) >> T_offset;

            load_list_a: for (int ii = o_begin_a; ii < o_end_a; ii++) {
        #pragma HLS pipeline ii=1
                list_a_temp = column_list[ii];
                int offset_loop = offset_a & 0xf - (ii - o_begin_a) * T;
                for (int jj = 0; jj < T; jj++) {
            #pragma HLS unroll
                    if (((jj + offset_loop) < T) && ((jj + offset_loop) >= 0)) {
                        list_a[j][jj] = list_a_temp.data[jj + offset_loop];
                        list_a_cache[jj] = list_a_temp.data[jj + offset_loop];
                    }
                }
            }
            list_a_cache_tag[0] = offset_a;
            list_a_tag[j] = offset_a;

    #if Profile==true
            miss_count_list_a++;
            memory_access_a += (o_end_a - o_begin_a);
            data_amount_off_chip_a += ((o_end_a - o_begin_a) * T);
    #endif

        }
    }
    a_element_out[0] = a_value;
}

// load list b function, contains 
// <1> request coalescing
// <2> fetch from off-chip memory (HBM);
// input:  b_request + column_list;
// output: list_b + b_element_out;

// no coalescing in load list b
void loadCpyListB (int dist_coal, para_int b_request, int512* column_list, 
                    int list_b_cache[CACHE_B_LEN][T], ap_int<512> list_b_cache_tag[CACHE_B_LEN >> 4], 
                    int list_b[Parallel][T], para_int b_element_out[1]) {

    para_int b_value = b_request;
#pragma HLS DATA_PACK variable=b_value
#pragma HLS ARRAY_PARTITION variable=b_value.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_value.length complete dim=1

    int512 list_b_temp; // need array partition for unroll
#pragma HLS ARRAY_PARTITION variable=list_b_temp complete dim=1

    load_list_b_Parallel: for (int j = 0; j < Parallel; j++) {

        int offset_b = b_value.offset[j];
        int length_b = b_value.length[j];

        int idx = offset_b & CACHE_B_MASK; // get the idx in cache
        int cacheline_index = idx >> 4;
        int inner_index = idx & 0xf;
        ap_int<32> cache_tag_ap = list_b_cache_tag[cacheline_index].range((inner_index + 1) * 32 - 1, inner_index * 32);
        int cache_tag = cache_tag_ap.to_int();

        if (cache_tag == offset_b) { // hit load from cache
            for (int jj = 0; jj < T; jj++) {
    #pragma HLS unroll
                list_b[j][jj] = list_b_cache[idx][jj]; 
            }
#if Profile==true
            hit_count_list_b++;
#endif

        } else {

            int o_begin_b = offset_b >> T_offset;
            int o_end_b = (offset_b + length_b + T - 1) >> T_offset;

            load_list_b: for (int ii = o_begin_b; ii < o_end_b; ii++) {
        #pragma HLS pipeline ii=1
                list_b_temp = column_list[ii];
                int offset_loop = offset_b & 0xf - (ii - o_begin_b) * T;
                for (int jj = 0; jj < T; jj++) {
            #pragma HLS unroll
                    if (((jj + offset_loop) < T) && ((jj + offset_loop) >= 0)) {
                        list_b[j][jj] = list_b_temp.data[jj + offset_loop];
                        list_b_cache[idx][jj] = list_b_temp.data[jj + offset_loop];
                    }
                }
            }
            list_b_cache_tag[cacheline_index].range((inner_index + 1) * 32 - 1, inner_index * 32) = offset_b;

#if Profile==true
            miss_count_list_b++;
            memory_access_b += (o_end_b - o_begin_b);
            data_amount_off_chip_b += ((o_end_b - o_begin_b) * T);
#endif
        }

        b_element_out[0].offset[j] = b_value.offset[j];
        b_element_out[0].length[j] = b_value.length[j];
    }
}

/*
int msb (unsigned int v) {
#pragma HLS inline
	static const int pos[32] = { 0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, \
                                4, 8, 31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9};
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v = (v >> 1) + 1;
	return pos[(v* 0x077CB531) >> 27];
}

int lbitBinarySearch(int list_b[T][BUF_DEPTH], int low, int high, int offset, int key){
#pragma HLS inline
    int init = offset & 0xf;
    int lbit = msb(high);

    int k = high ? 1 << lbit : 0;
    int idx_k = k + init;
    int block_k = idx_k >> 4;
    int bias_k = idx_k & 0xf;
    int temp_value = list_b[bias_k][block_k];

    int r, idx_r, block_r, bias_r;
    int i = (temp_value <= key) ? k : 0;
    int idx_i, block_i, bias_i;
    while ((k >>= 1)) {
#pragma HLS pipeline
        r = i + k;
        idx_r = r + init;
        block_r = idx_r >> 4;
        bias_r = idx_r & 0xf;
        temp_value = list_b[bias_r][block_r];

        if ((r <= high) && (key > temp_value)) {
            i = r;
        } else if ((r <= high) && (key == temp_value)) {
            return r;
        }
    }

    idx_i = i + init;
    block_i = idx_i >> 4;
    bias_i = idx_i & 0xf;
    temp_value = list_b[bias_i][block_i];
    if (temp_value == key) {
        return i;
    } else {
        return -1;
    }
}

void setIntersectionBiSearch(int list_a[T][BUF_DEPTH], int list_b[T][BUF_DEPTH], int list_a_offset, int list_a_len, \
                     int list_b_offset, int list_b_len, int* tc_count) 
{
#pragma HLS inline
    // Need to make sure the list_a is the shorter one
    int count = 0;
    int temp_idx_0 = 0;
    // list_a has to be the shorter one, search with element from shorter array
    setBiSearch: for (int i = 0; i < list_a_len; i++) {
        int ini_idx_a = list_a_offset & 0xf;
        int idx = ini_idx_a + i;
        int a_dim_1 = idx >> 4;
        int a_dim_2 = idx & 0xf;
        int a_value = list_a[a_dim_2][a_dim_1];
        temp_idx_0 = lbitBinarySearch(list_b, 0, list_b_len-1, list_b_offset, a_value);
        if (temp_idx_0 != -1) {
            count++;
        }
    }
    tc_count[0] = count;
}
*/

void setIntersectionMerge(int list_a[T], int list_b[T], int list_a_offset, int list_a_len, \
                     int list_b_offset, int list_b_len, int* tc_count)
{
#pragma HLS inline
    // int o_idx_a = list_a_offset & 0xf;
    // int o_idx_b = list_b_offset & 0xf;
    int o_idx_a = 0;
    int o_idx_b = 0;
    int idx_a = o_idx_a;
    int idx_b = o_idx_b;
    int count = 0;  
loop_set_insection: while ((idx_a < list_a_len + o_idx_a) && (idx_b < list_b_len + o_idx_b))
    {
#pragma HLS pipeline ii=1
        // int a_dim_1 = idx_a >> T_offset;
        // int a_dim_2 = idx_a & 0xf;

        // int b_dim_1 = idx_b >> T_offset;
        // int b_dim_2 = idx_b & 0xf;

        // int value_a = list_a[a_dim_2][a_dim_1];
        // int value_b = list_b[b_dim_2][b_dim_1];
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
    tc_count[0] = count;
}

void setIntersection (int list_a[T], int list_b[T], int list_a_offset, int list_a_len, \
                     int list_b_offset, int list_b_len, int* tc_num) {
#pragma HLS inline off

    setIntersectionMerge(list_a, list_b, list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);

    // if (list_b_len >= (list_a_len << 5)) {
    //     setIntersectionBiSearch(list_a, list_b, list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);
    // } else if (list_a_len >= (list_b_len << 5)) {
    //     setIntersectionBiSearch(list_b, list_a, list_b_offset, list_b_len, list_a_offset, list_a_len, tc_num);
    // } else {
    //     setIntersectionMerge(list_a, list_b, list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);
    // }
}

void processList(int list_a[Parallel][T], int list_b[Parallel][T], 
                 para_int a_input[1], para_int b_input[1], int* tc_count) {
#pragma HLS inline off
    int tri_count = 0;
    int temp_result[Parallel];
#pragma HLS array_partition variable=temp_result type=complete dim=1

    para_int list_a_input = a_input[0];
    para_int list_b_input = b_input[0];

    process_list: for (int j = 0; j < Parallel; j++) {
#pragma HLS unroll
        int list_a_offset = list_a_input.offset[j];
        int list_a_len = list_a_input.length[j];
        int list_b_offset = list_b_input.offset[j];
        int list_b_len = list_b_input.length[j];
        int tc_num[1] = {0};
        setIntersection(list_a[j], list_b[j], list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);        
        temp_result[j] = tc_num[0];
    }

    process_result_sum: for (int j = 0; j < Parallel; j++) {
        tri_count += temp_result[j];
    }

    tc_count[0] = tri_count;
}


extern "C" {
void TriangleCount (int512* edge_list, int64* offset_list, int512* column_list_1, int512* column_list_2, int edge_num, int dist_coal, int* tc_number ) {

#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 32 max_read_burst_length = 16 bundle = gmem0 port = edge_list
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 16 max_read_burst_length = 64 bundle = gmem1 port = offset_list
#pragma HLS INTERFACE m_axi offset = slave latency = 4 num_read_outstanding = 2 max_read_burst_length = 2 bundle = gmem2 port = column_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 4 num_read_outstanding = 2 max_read_burst_length = 2 bundle = gmem2 port = column_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 4 max_write_burst_length = 2 bundle = gmem0 port = tc_number

#pragma HLS INTERFACE s_axilite port = edge_list bundle = control
#pragma HLS INTERFACE s_axilite port = offset_list bundle = control
#pragma HLS INTERFACE s_axilite port = column_list_1 bundle = control
#pragma HLS INTERFACE s_axilite port = column_list_2 bundle = control
#pragma HLS INTERFACE s_axilite port = edge_num bundle = control
#pragma HLS INTERFACE s_axilite port = dist_coal bundle = control
#pragma HLS INTERFACE s_axilite port = tc_number bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS dataflow

    static hls::stream<int512> edgeStrm;
#pragma HLS STREAM variable = edgeStrm depth=128
    static hls::stream<bool> ctrlStrm;
#pragma HLS STREAM variable = ctrlStrm depth=64
    static hls::stream<para_int> offLenStrmA;
#pragma HLS STREAM variable = offLenStrmA depth=64
    static hls::stream<para_int> offLenStrmB;
#pragma HLS STREAM variable = offLenStrmB depth=64

    static hls::stream<bool> ctrlStrmS;
#pragma HLS STREAM variable = ctrlStrmS depth=16
    static hls::stream<para_int> offLenStrmAS;
#pragma HLS STREAM variable = offLenStrmAS depth=16
    static hls::stream<para_int> offLenStrmBS;
#pragma HLS STREAM variable = offLenStrmBS depth=16

    int list_a_ping[Parallel][T];
    int list_b_ping[Parallel][T];
    int list_a_pong[Parallel][T];
    int list_b_pong[Parallel][T];
#pragma HLS array_partition variable=list_a_ping type=complete dim=1
#pragma HLS array_partition variable=list_a_ping type=complete dim=2
#pragma HLS array_partition variable=list_b_ping type=complete dim=1 
#pragma HLS array_partition variable=list_b_ping type=complete dim=2
#pragma HLS array_partition variable=list_a_pong type=complete dim=1
#pragma HLS array_partition variable=list_a_pong type=complete dim=2
#pragma HLS array_partition variable=list_b_pong type=complete dim=1 
#pragma HLS array_partition variable=list_b_pong type=complete dim=2

    int list_a_cache[T]; // size = 1
    int list_b_cache[CACHE_B_LEN][T]; // list b cache size = CACHE_B_LEN
#pragma HLS array_partition variable=list_a_cache type=complete dim=1
#pragma HLS array_partition variable=list_b_cache type=complete dim=2
#pragma HLS BIND_STORAGE variable=list_b_cache type=RAM_S2P impl=BRAM

    int list_a_cache_tag[1];
    ap_int<512> list_b_cache_tag[CACHE_B_LEN >> 4]; // each 512 bit data contains 16 cache tag. 
#pragma HLS array_partition variable=list_b_cache_tag type=complete dim=1
    list_a_cache_tag[0] = -1;
    for (int i = 0; i < (CACHE_B_LEN >> 4); i++) {
        list_b_cache_tag[i] = ~list_b_cache_tag[i]; // initial to 1 for all bits
    }

    int list_a_pong_tag[Parallel]; // indicate list_a value, if hit, no need to copy.
    int list_a_ping_tag[Parallel];
#pragma HLS array_partition variable=list_a_pong_tag type=complete dim=1
#pragma HLS array_partition variable=list_a_ping_tag type=complete dim=1
    for (int i = 0; i < Parallel; i++) {
        list_a_pong_tag[i] = -1;
        list_a_ping_tag[i] = -1;
    }

    int triCount_ping[1]={0};
    int triCount_pong[1]={0};
    int length = edge_num*2;

    loadEdgeList (length, edge_list, edgeStrm);
    loadOffset (length, offset_list, edgeStrm, offLenStrmA, offLenStrmB);
    elistTaskSplitter(length, offLenStrmA, offLenStrmB, 
                      ctrlStrmS, offLenStrmAS, offLenStrmBS);

    int pp = 0; // ping-pong operation
    int TC_ping = 0;
    int TC_pong = 0;
    bool strm_control;

    // need to be parallel processed, complete partition
    para_int a_ele_out_ping[1];
    para_int a_ele_out_pong[1];
    para_int b_ele_out_ping[1];
    para_int b_ele_out_pong[1];

    // strm_control = ctrlStrmS.read();

    pp_load_cpy_process: while(1) {

#pragma HLS pipeline

        strm_control = ctrlStrmS.read();
        para_int a_ele_in = offLenStrmAS.read();
        para_int b_ele_in = offLenStrmBS.read();
/*
        if (pp) {
            processList (list_a_ping, list_b_ping, a_ele_out_pong, b_ele_out_pong, triCount_pong);
            loadCpyListA (a_ele_in, column_list_1, list_a_cache, list_a_cache_tag, list_a_pong, list_a_pong_tag, a_ele_out_ping);
            loadCpyListB (dist_coal, b_ele_in, column_list_2, list_b_pong, b_ele_out_ping);
            TC_pong += triCount_pong[0];
        } else {
            processList (list_a_pong, list_b_pong, a_ele_out_ping, b_ele_out_ping, triCount_ping);
            loadCpyListA (a_ele_in, column_list_1, list_a_cache, list_a_cache_tag, list_a_ping, list_a_ping_tag, a_ele_out_pong);
            loadCpyListB (dist_coal, b_ele_in, column_list_2, list_b_ping, b_ele_out_pong);
            TC_ping += triCount_ping[0];
        }
        pp = 1 - pp;
*/
        loadCpyListA (a_ele_in, column_list_1, list_a_cache, list_a_cache_tag, list_a_ping, list_a_ping_tag, a_ele_out_ping);
        loadCpyListB (dist_coal, b_ele_in, column_list_2, list_b_cache, list_b_cache_tag, list_b_ping, b_ele_out_ping);
        processList (list_a_ping, list_b_ping, a_ele_out_ping, b_ele_out_ping, triCount_ping);
        TC_ping += triCount_ping[0];

        if (strm_control == false) {
            break;
        }
    }
#if Profile==true
    std::cout << "Parallel size = " << Parallel << std::endl;
    std::cout << "hit_count_list_a = " << hit_count_list_a << std::endl;
    std::cout << "miss_count_list_a = " << miss_count_list_a << std::endl;
    std::cout << "hit_count_list_b = " << hit_count_list_b << std::endl;
    std::cout << "miss_count_list_b = " << miss_count_list_b << std::endl;
    std::cout << "memory_access_a = " << memory_access_a << std::endl;
    std::cout << "memory_access_b = " << memory_access_b << std::endl;
    std::cout << "data_amount_off_chip_a = " << data_amount_off_chip_a << std::endl;
    std::cout << "data_amount_off_chip_b = " << data_amount_off_chip_b << std::endl;
#endif

    tc_number[0] = TC_ping + TC_pong;
}
}
