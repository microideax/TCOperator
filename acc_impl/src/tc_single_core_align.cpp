// refactor on 1-12 refactor, add ping-pong mode and set the load granuality as 16 vertex
#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>
#include <algorithm>
#include "ap_int.h"

#define Profile         true // used for sw_emu
#define T               16
#define T_offset        4
#define E_per_burst     8
#define Parallel        8
/*cache in load list*/
#define OFFSET_CACHE_SIZE   512
#define OFFSET_CACHE_OFFSET 9
#define OFFSET_CACHE_MASK   0x1ff
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

void loadOffset(int length, int64* offset_list, hls::stream<int512>& eStrmIn, \
                hls::stream<bool>& ctrlStrm, hls::stream<para_int>& StrmA, hls::stream<para_int>& StrmB) {
// cache init; use 1 BRAM for len storage and (OFFSET_CACHE_SIZE>>4) ap_int<512> for offset tag;

    ap_int<512> tagCache[OFFSET_CACHE_SIZE >> 4]; // each 512 bit data contains 16 cache tag. 
#pragma HLS array_partition variable=tagCache type=complete dim=1
    for (int i = 0; i < (OFFSET_CACHE_SIZE >> 4); i++) {
#pragma HLS unroll
        tagCache[i] = -1; // initial to 1 for all bits
    }

    int offsetCache[OFFSET_CACHE_SIZE]; // BRAM 18K : 36 bit * 512;
#pragma HLS BIND_STORAGE variable=offsetCache type=RAM_S2P impl=BRAM
    int lengthCache[OFFSET_CACHE_SIZE]; // BRAM 18K : 36 bit * 512;
#pragma HLS BIND_STORAGE variable=lengthCache type=RAM_S2P impl=BRAM

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
    int item_value, item_offset, item_length;
    int a_offset, a_length;
    int b_offset, b_length;
    int idx, cacheline_index, inner_index, cache_tag; // for cache use
    ap_int<32> cache_tag_ap;

    for (int i = 0; i < loop; i++) {
        edge_value = eStrmIn.read();
#pragma HLS array_partition variable=edge_value type=complete dim=1

        for (int j = 0; j < E_per_burst * 2; j++) {
#pragma HLS pipeline II=1 // this pipeline has an II=3 and depth=28 because of the cacheLoad function, TO be optimized
            if ((i*T + j) < length) {
                // load a or b; j%2 == 0 : a; else b;
                item_value = edge_value.data[j];
                idx = item_value & OFFSET_CACHE_MASK; // get the idx in cache
                cacheline_index = idx >> 4;
                inner_index = idx & 0xf;
                cache_tag_ap = tagCache[cacheline_index].range((inner_index + 1) * 32 - 1, inner_index * 32);
                cache_tag = cache_tag_ap.to_int();
                if (cache_tag == item_value) { // hit 
                    // std::cout << "hit" << std::endl;
                    item_offset = offsetCache[idx];
                    item_length = lengthCache[idx];
                } else {
                    // std::cout << "miss" << std::endl;
                    int64 temp_value = offset_list[item_value];
                    item_offset = temp_value.data[0];
                    item_length = temp_value.data[1] - temp_value.data[0];
                    offsetCache[idx] = item_offset;
                    lengthCache[idx] = item_length;
                    tagCache[cacheline_index].range((inner_index + 1) * 32 - 1, inner_index * 32) = item_value;
                }

                if (j % 2 == 0) {
                    a_offset = item_offset;
                    a_length = item_length;
                } else {
                    b_offset = item_offset;
                    b_length = item_length;
                }

                // std::cout << "a_value, b_value: " << a_value << ", " << b_value << std::endl;
                // std::cout << "Loaded a_offset, b_offset: " << a_offset << ", " << b_offset << std::endl;
                // std::cout << "Loaded a_length, b_length: " << a_length << ", " << b_length << std::endl;

                if ((j % 2 == 1) && (a_length > 0) && (b_length > 0)) {

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

// loadCpyListA function contains :  
// <1> hit-miss check; 
// <2> load data from off-chip memory or cache;
// <3> update cache
// input:  column_list + a_offset + a_length + list_a_cache + list_a_cache_tag + list_a_tag;
// output: list_a + list_a_tag;
void loadCpyListA ( para_int a_element_in, int512* column_list, 
                    int list_a_cache[T], int list_a_cache_tag[1], 
                    int list_a[Parallel][T], int list_a_tag[Parallel],
                    int a_length_out[Parallel]) {

    para_int a_value = a_element_in;
#pragma HLS DATA_PACK variable=a_value
#pragma HLS ARRAY_PARTITION variable=a_value.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=a_value.length complete dim=1

    int list_cache_tag = list_a_cache_tag[0];
    int512 list_a_temp; // need array partition for unroll
#pragma HLS ARRAY_PARTITION variable=list_a_temp complete dim=1

    load_copy_list_a_Parallel: for (int j = 0; j < Parallel; j++) {
#pragma HLS pipeline

        int offset_a = a_value.offset[j];
        int length_a = a_value.length[j];

        if ((length_a <= 0)||(offset_a < 0)) {
            continue;
        }
        
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
            list_a_temp = column_list[o_begin_a];
            for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                list_a[j][jj] = list_a_temp.data[jj];
                list_a_cache[jj] = list_a_temp.data[jj];
            }
            list_a_cache_tag[0] = offset_a;
            list_a_tag[j] = offset_a;

    #if Profile==true
            miss_count_list_a++;
            memory_access_a += 1;
            data_amount_off_chip_a += T;
    #endif

        }
    }
    for (int i = 0; i < Parallel; i++) {
#pragma HLS unroll
        a_length_out[i] = a_value.length[i];
    }
}

// load list b function, contains 
// <1> request coalescing
// <2> fetch from off-chip memory (HBM);
// input:  b_request + column_list;
// output: list_b + b_element_out;

// no coalescing in load list b
void loadCpyListB (int dist_coal, para_int b_request, int512* column_list, 
                    int list_b_cache[CACHE_B_LEN][T], ap_int<512> list_b_cache_tag[CACHE_B_LEN >> 4], 
                    int list_b[Parallel][T], int b_length_out[Parallel]) {

    para_int b_value = b_request;
#pragma HLS DATA_PACK variable=b_value
#pragma HLS ARRAY_PARTITION variable=b_value.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_value.length complete dim=1

    int512 list_b_temp; // need array partition for unroll
#pragma HLS ARRAY_PARTITION variable=list_b_temp complete dim=1

    load_list_b_Parallel: for (int j = 0; j < Parallel; j++) {
#pragma HLS pipeline

        int offset_b = b_value.offset[j];
        int length_b = b_value.length[j];

        if ((length_b <= 0)||(offset_b < 0)) {
            continue;
            
        } 

        int idx = (offset_b & CACHE_B_MASK); // get the idx in cache
        int cacheline_index = idx >> 4;
        int inner_index = (idx & 0xf);
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
            list_b_temp = column_list[o_begin_b];
            for (int jj = 0; jj < T; jj++) {
        #pragma HLS unroll
                list_b[j][jj] = list_b_temp.data[jj];
                list_b_cache[idx][jj] = list_b_temp.data[jj];
            }
            list_b_cache_tag[cacheline_index].range((inner_index + 1) * 32 - 1, inner_index * 32) = offset_b;

#if Profile==true
            miss_count_list_b++;
            memory_access_b += 1;
            data_amount_off_chip_b += T;
#endif
        }
    }
    for (int i = 0; i < Parallel; i++) {
#pragma HLS unroll
        b_length_out[i] = b_value.length[i];
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

void setIntersectionMerge(int list_a[T], int list_b[T], int list_a_len, \
                            int list_b_len, int* tc_count)
{
#pragma HLS inline
    int idx_a = 0;
    int idx_b = 0;
    int count = 0;  
loop_set_insection: while ((idx_a < list_a_len) && (idx_b < list_b_len))
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
    tc_count[0] = count;
}

void setIntersection (int list_a[T], int list_b[T], int list_a_len, \
                        int list_b_len, int* tc_num) {
#pragma HLS inline off

    setIntersectionMerge(list_a, list_b, list_a_len, list_b_len, tc_num);

    // if (list_b_len >= (list_a_len << 5)) {
    //     setIntersectionBiSearch(list_a, list_b, list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);
    // } else if (list_a_len >= (list_b_len << 5)) {
    //     setIntersectionBiSearch(list_b, list_a, list_b_offset, list_b_len, list_a_offset, list_a_len, tc_num);
    // } else {
    //     setIntersectionMerge(list_a, list_b, list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);
    // }
}

void processList(int list_a[Parallel][T], int list_b[Parallel][T], 
                int a_length_in[Parallel], int b_length_in[Parallel], 
                 int* tc_count) {
#pragma HLS inline off
    int tri_count = 0;
    int temp_result[Parallel];
#pragma HLS array_partition variable=temp_result type=complete dim=1

    process_list: for (int j = 0; j < Parallel; j++) {
#pragma HLS unroll
        int list_a_len = a_length_in[j];
        int list_b_len = b_length_in[j];
        int tc_num[1] = {0};
        setIntersection(list_a[j], list_b[j], list_a_len, list_b_len, tc_num);
        temp_result[j] = tc_num[0];
    }

    process_result_sum: for (int j = 0; j < Parallel; j++) {
        tri_count += temp_result[j];
    }

    tc_count[0] = tri_count;
}


extern "C" {
void TriangleCount (int512* edge_list, int64* offset_list, int512* column_list_1, int512* column_list_2, int edge_num, int dist_coal, int* tc_number ) {

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
#pragma HLS INTERFACE s_axilite port = dist_coal bundle = control
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
#pragma HLS unroll
        list_b_cache_tag[i] = -1; // initial to 1 for all bits
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

    int a_length_ping[Parallel];
    int a_length_pong[Parallel];
    int b_length_ping[Parallel];
    int b_length_pong[Parallel];
#pragma HLS array_partition variable=a_length_ping type=complete dim=1
#pragma HLS array_partition variable=a_length_pong type=complete dim=1
#pragma HLS array_partition variable=b_length_ping type=complete dim=1
#pragma HLS array_partition variable=b_length_pong type=complete dim=1
    for (int i = 0; i < Parallel; i++) {
#pragma HLS unroll
        a_length_ping[i] = 0;
        a_length_pong[i] = 0;
        b_length_ping[i] = 0;
        b_length_pong[i] = 0;
    }

    int length = edge_num*2;

    loadEdgeList (length, edge_list, edgeStrm);
    loadOffset (length, offset_list, edgeStrm, ctrlStrm, offLenStrmA, offLenStrmB);

    int pp = 0; // ping-pong operation
    int TC_ping = 0;
    int TC_pong = 0;
    bool strm_control;

    pp_load_cpy_process: while(1) {

#pragma HLS pipeline

        strm_control = ctrlStrm.read();
        para_int a_ele_in = offLenStrmA.read();
        para_int b_ele_in = offLenStrmB.read();

        // if (pp) {
        //     processList (list_a_ping, list_b_ping, a_length_pong, b_length_pong, triCount_pong);
        //     loadCpyListA (a_ele_in, column_list_1, list_a_cache, list_a_cache_tag, list_a_pong, list_a_pong_tag, a_length_ping);
        //     loadCpyListB (dist_coal, b_ele_in, column_list_2, list_b_cache, list_b_cache_tag, list_b_pong, b_length_ping);
        //     TC_pong += triCount_pong[0];
        // } else {
        //     processList (list_a_pong, list_b_pong, a_length_ping, b_length_ping, triCount_ping);
        //     loadCpyListA (a_ele_in, column_list_1, list_a_cache, list_a_cache_tag, list_a_ping, list_a_ping_tag, a_length_pong);
        //     loadCpyListB (dist_coal, b_ele_in, column_list_2, list_b_cache, list_b_cache_tag, list_b_ping, b_length_pong);
        //     TC_ping += triCount_ping[0];
        // }
        // pp = 1 - pp;

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
