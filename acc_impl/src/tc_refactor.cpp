// refactor on 6-7-2023
#include <stdint.h>
#include <hls_stream.h>
#include <hls_math.h>
#include <iostream>
#include <algorithm>
                                                                             
#define BUF_DEPTH   512
#define T           16
#define T_offset    4
#define E_per_burst 8
#define Parallel    12
#define Cache_size  16
#define debug       true

typedef struct data_512bit_type {int data[16];} int512; // for off-chip memory data access.
typedef struct data_custom_type {
    int offset[Parallel];
    int length[Parallel];
} para_int; // for parallel processing.
typedef struct data_request_type {
    int status[Parallel];
    int cache_id[Parallel];
    int offset[Parallel];
    int length[Parallel];
} req_int; // for cache request.


void loadEdgeList(int length, int512* inArr, hls::stream<int512>& eStrmOut) {
    int loop = (length + T - 1) / T;
    for (int i = 0; i < loop; i++) {
#pragma HLS pipeline ii = 1
        eStrmOut << inArr[i];
    }
}

void loadOffset(int length, int* offset_list_1, int* offset_list_2, 
                hls::stream<int512>& eStrmIn, hls::stream<bool>& ctrlStrm,
                hls::stream<para_int>& StrmA, hls::stream<para_int>& StrmB) {
    int loop = (length + T - 1) / T;
    int count = 0; // need to identify

    para_int a_para;
    para_int b_para;
#pragma HLS DATA_PACK variable=a_para
#pragma HLS ARRAY_PARTITION variable=a_para.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=a_para.length complete dim=1
#pragma HLS DATA_PACK variable=b_para
#pragma HLS ARRAY_PARTITION variable=b_para.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_para.length complete dim=1
 
    for (int i = 0; i < loop; i++) {
        int512 edge_value = eStrmIn.read();
#pragma HLS array_partition variable=edge_value type=complete dim=1

        int a_value, a_offset, a_length;
        int b_value, b_offset, b_length;
        for (int j = 0; j < E_per_burst; j++) {
            if (i*T + 2*j < length) {
                a_value = edge_value.data[j*2];
                a_offset = offset_list_1[a_value];
                a_length = offset_list_1[a_value + 1] - a_offset;
                b_value = edge_value.data[j*2 + 1];
                b_offset = offset_list_2[b_value];
                b_length = offset_list_2[b_value + 1] - b_offset;
            } else {
                a_offset = 0;
                a_length = 0;
                b_offset = 0;
                b_length = 0;
            }

            if ((a_length > 0) && (b_length > 0)) {
                // make sure length > 0
                a_para.offset[count] = a_offset;
                a_para.length[count] = a_length;
                b_para.offset[count] = b_offset;
                b_para.length[count] = b_length;
                count ++;
            }

            if (count == Parallel) {
                count = 0;
                StrmA << a_para;
                StrmB << b_para;
                ctrlStrm << true; // not the end stream
            }
        }
    }

    // process ping-pong corner case;
    for (int i = count; i < Parallel; i++) {
        a_para.offset[i] = 0;
        a_para.length[i] = 0;
        b_para.offset[i] = 0;
        b_para.length[i] = 0;
    }
    StrmA << a_para;
    StrmB << b_para;
    ctrlStrm << true; // not the end stream
    StrmA << a_para;
    StrmB << b_para;
    ctrlStrm << false; // end stream: the last stream
}

// loadCpyListA function contains :  
// <1> hit-miss check; 
// <2> load data from off-chip memory or cache;
// <3> update cache
// input:  column_list_1 + a_offset + a_length + list_a_cache + list_a_cache_tag + list_a_tag;
// output: list_a + list_a_tag;
void loadCpyListA ( para_int a_element_in, int512* column_list_1, 
                    int list_a_cache[1][T][BUF_DEPTH], int list_a_cache_tag[2], 
                    int list_a[Parallel][T][BUF_DEPTH], int list_a_tag[Parallel],
                    para_int a_element_out[1]) {

    para_int a_value = a_element_in;
#pragma HLS DATA_PACK variable=a_value

    int list_cache_tag = list_a_cache_tag[0];
    int len_reg = list_a_cache_tag[1];
    int list_tag = -1;

    load_copy_list_a_Parallel: for (int j = 0; j < Parallel; j++) {
    list_tag = list_a_tag[j];
    if (a_value.offset[j] == list_tag) {
        // hit, no need to copy data
        continue;
    } else if (a_value.offset[j] == list_cache_tag) { // list_a_cache_tag[0]: value
        // hit, copy data from list_a cache
        list_a_tag[j] = list_cache_tag;
        copy_list_a: for (int ii = 0; ii < len_reg; ii++) { // list_a_tag[1]: length
#pragma HLS pipeline
            for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                list_a[j][jj][ii] = list_a_cache[0][jj][ii];
            }
        }
    } else {
        // miss, load data from off-chip memory and update cache
        int o_begin_a = a_value.offset[j] >> T_offset;
        int o_end_a = (a_value.offset[j] + a_value.length[j] + T - 1) >> T_offset;

        load_list_a: for (int ii = o_begin_a; ii < o_end_a; ii++) {
#pragma HLS pipeline
            int512 list_a_temp = column_list_1[ii];
            for (int jj = 0; jj < T; jj++) {
    #pragma HLS unroll
                list_a[j][jj][ii - o_begin_a] = list_a_temp.data[jj];
                list_a_cache[0][jj][ii - o_begin_a] = list_a_temp.data[jj];
            }
        }
        list_a_cache_tag[0] = a_value.offset[j];
        list_cache_tag = a_value.offset[j];
        list_a_cache_tag[1] = o_end_a - o_begin_a;
        len_reg = o_end_a - o_begin_a;
        list_a_tag[j] = a_value.offset[j];
    }
    }
    a_element_out[0] = a_value;
}


// preprocess load b function contains :
// <1> hit-miss check;
// <2> coalescing missed request;
// <3> write b_status and update b_cache_tag;
// input:  b_offset + b_length + b_cache_tag;
// output: b_status + b_cache_tag;
// Note: b_status is the memory requestor;
void preLoadListB (hls::stream<para_int>& StrmB_in, hls::stream<bool>& ctrlStrm_in,
                   hls::stream<bool>& ctrlStrm_out, hls::stream<req_int>& StrmB_out) {

    int list_b_cache_tag[Cache_size]; // cache tag array
#pragma HLS ARRAY_PARTITION variable=list_b_cache_tag complete dim=1
    for (int i = 0; i < Cache_size; i++) {
#pragma HLS unroll
        list_b_cache_tag[i] = -1;
    }

    req_int b_element_out;
#pragma HLS DATA_PACK variable=b_element_out
#pragma HLS ARRAY_PARTITION variable=b_element_out.status complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_element_out.cache_id complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_element_out.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_element_out.length complete dim=1

    while (1) {
#pragma HLS pipeline

        para_int b_element_in = StrmB_in.read();
#pragma HLS DATA_PACK variable=b_element_in
#pragma HLS ARRAY_PARTITION variable=b_element_in.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_element_in.length complete dim=1

        // <1> hit-miss check
        int hit_idx = 0;
        int miss_idx = 0;
        int need_coalsece[Parallel];
        bool cache_hit = false;
        for (int i = 0; i < Parallel; i++) {
#pragma HLS pipeline
            int tag_temp = b_element_in.offset[i];
            for (int j = 0; j < Cache_size; j++) {
#pragma HLS unroll
                if (tag_temp == list_b_cache_tag[j]) {
                    b_element_out.status[hit_idx] = 0x7f; // hit
                    b_element_out.cache_id[hit_idx] = j; // cacheline id, copy from cache.
                    cache_hit = true;
                }
            }
            if (cache_hit) {
                hit_idx ++;
            } else {
                need_coalsece[miss_idx] = i; // collect missed requests.
                miss_idx ++;
            }
        }
        // double check
        if ((hit_idx + miss_idx) != Parallel) {
            std::cout << " cache hit miss number is not correct! " << std::endl;
            exit(-1); // error
        }

        // <2> coalescing missed request;
        bool coalescing = false; // initialize
        int out_idx = hit_idx;
        for (int i = 0; i < miss_idx; i++) {
#pragma HLS pipeline
            int ori_idx = need_coalsece[i]; // input index
            if (i == (miss_idx - 1)) {
                if (coalescing == false) {
                    b_element_out.status[out_idx] = 0x6f; // single load, no coalescing 
                } else {
                    b_element_out.status[out_idx] = 0x4f; // coalescing end flag;
                }
            } else {
                int nxt_idx = need_coalsece[i+1]; // next index
                int distance = b_element_in.offset[nxt_idx] - b_element_in.offset[ori_idx] - b_element_in.length[ori_idx];
                bool coales_status = (distance > 64)? true: false; // check next status
                if (coalescing == false) {
                    if (coales_status) {
                        b_element_out.status[out_idx] = 0x6f; // single load, distance > threshold
                    } else {
                        b_element_out.status[out_idx] = 0x2f; // coalescing start flag;
                        coalescing = true;
                    }
                } else {
                    if (coales_status) {
                        b_element_out.status[out_idx] = 0x4f; // coalescing end flag;
                        coalescing = false;
                    } else {
                        b_element_out.status[out_idx] = 0x3f; // coalescing middle flag;
                    }
                }
            }
            b_element_out.offset[out_idx] = b_element_in.offset[ori_idx];
            b_element_out.length[out_idx] = b_element_in.length[ori_idx];
            out_idx++;
        }

        // double check
        if (out_idx != Parallel) {
            std::cout << " cache hit miss number is not correct! " << std::endl;
            exit(-1); // error
        }

        StrmB_out << b_element_out;

        bool control_sig = ctrlStrm_in.read();
        ctrlStrm_out << control_sig;
        if (control_sig == false) {
            break;
        }

        // <3> update cache_tag
        for (int i = 0; i < Cache_size; i++) {
#pragma HLS unroll
            list_b_cache_tag[i] = -1; // at current stage, disable cache;
        }
    }
}


// load list b function, contains 
// <1> fetch from off-chip memory (HBM) or cache to list_b;
// <2> update cache data
// input:  b_request + column_list_2 + b_cache_data;
// output: list_b + b_element_out;
void loadListB (req_int b_request, int512* column_list_2, int b_cache_data[Cache_size][T][BUF_DEPTH], 
                int list_b[Parallel][T][BUF_DEPTH], para_int b_element_out[1]) {

    req_int b_req_in = b_request;
#pragma HLS DATA_PACK variable=b_req_in
#pragma HLS ARRAY_PARTITION variable=b_req_in.status complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_req_in.cache_id complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_req_in.offset complete dim=1
#pragma HLS ARRAY_PARTITION variable=b_req_in.length complete dim=1

    int item_begin[Parallel];
    int item_end[Parallel];
    int list_temp[T];
#pragma HLS array_partition variable=item_begin type=complete dim=1
#pragma HLS array_partition variable=item_end type=complete dim=1
#pragma HLS array_partition variable=list_temp type=complete dim=1

    for (int t = 0; t < Parallel; t++) {
#pragma HLS unroll
        item_begin[t] = (b_req_in.offset[t]) >> T_offset;
        item_end[t] = (b_req_in.offset[t] + b_req_in.length[t] + T - 1) >> T_offset;
        b_element_out[0].offset[t] = b_req_in.offset[t];
        b_element_out[0].length[t] = b_req_in.length[t];
    }

    // <1> fetch from off-chip memory or cache to list_b;
    int coalesce_begin = 0;
    int coalesce_end   = 0;
    for (int i = 0; i < Parallel; i++) {
#pragma HLS pipeline
        int status = b_req_in.status[i];
        if (status == 0x6f) { // single load, no coalescing
            for (int ii = item_begin[i]; ii < item_end[i]; ii++) {
#pragma HLS pipeline
                int512 list_b_temp = column_list_2[ii];
                for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                    list_b[i][jj][ii - item_begin[i]] = list_b_temp.data[jj];
                }
            }
        } else if (status == 0x7f) { // hit, fetch data from cache
            int cacheline_id = b_req_in.cache_id[i];
            for (int ii = item_begin[i]; ii < item_end[i]; ii++) {
#pragma HLS pipeline
                for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                    list_b[i][jj][ii - item_begin[i]] = b_cache_data[cacheline_id][jj][ii - item_begin[i]];
                }
            }
        } else if (status == 0x2f) { // coalescing start
            coalesce_begin = item_begin[i];
            coalesce_end = item_end[i];
        } else if (status == 0x3f) { // coalescing middle
            coalesce_begin = (item_begin[i] < coalesce_begin)? item_begin[i]: coalesce_begin;
            coalesce_end = (item_end[i] > coalesce_end)? item_end[i]: coalesce_end;
            continue;
        } else if (status == 0x4f) { // coalescing end
            coalesce_begin = (item_begin[i] < coalesce_begin)? item_begin[i]: coalesce_begin;
            coalesce_end = (item_end[i] > coalesce_end)? item_end[i]: coalesce_end;
            load_list_b: for (int ii = coalesce_begin; ii < coalesce_end; ii++) {
#pragma HLS pipeline
                int512 list_b_temp = column_list_2[ii];
                for (int tt = 0; tt < Parallel; tt++) {
#pragma HLS unroll
                    for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                        list_temp[jj] = list_b_temp.data[jj];
                        if ((ii >= item_begin[tt]) && (ii < item_end[tt])) {
                            list_b[tt][jj][ii - item_begin[tt]] = list_temp[jj];
                        }
                    }
                }
            }
            coalesce_begin = 0;
            coalesce_end   = 0;
        } else {
            std::cout << " Error status list b ! " << std::endl;
        }
    }

    // <2> update cache data
    // Need to identify cache policy, at current stage no update 
}


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
/** Need to make sure the list_a is the shorter one **/
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

void setIntersectionMerge(int list_a[T][BUF_DEPTH], int list_b[T][BUF_DEPTH], int list_a_offset, int list_a_len, \
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

void setIntersection (int list_a[T][BUF_DEPTH], int list_b[T][BUF_DEPTH], int list_a_offset, int list_a_len, \
                     int list_b_offset, int list_b_len, int* tc_num) {
#pragma HLS inline off

    if (list_b_len >= (list_a_len << 5)) {
        setIntersectionBiSearch(list_a, list_b, list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);
    } else if (list_a_len >= (list_b_len << 5)) {
        setIntersectionBiSearch(list_b, list_a, list_b_offset, list_b_len, list_a_offset, list_a_len, tc_num);
    } else {
        setIntersectionMerge(list_a, list_b, list_a_offset, list_a_len, list_b_offset, list_b_len, tc_num);
    }
}

void processList(int list_a[Parallel][T][BUF_DEPTH], int list_b[Parallel][T][BUF_DEPTH], 
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
#if debug == true
        // print list a and list b
        int a_offset_temp = list_a_offset & 0xf;
        std::cout << "list a = ";
        for (int a_temp = 0; a_temp < list_a_len; a_temp++) {
            int a_idx = a_temp + a_offset_temp;
            int a_1_dim = a_idx & 0xf;
            int a_2_dim = a_idx >> T_offset;
            std::cout << list_a[j][a_1_dim][a_2_dim] << " ";
        }
        std::cout << " a_len = " << list_a_len << " offset = " << list_a_offset << std::endl;

        int b_offset_temp = list_b_offset & 0xf;
        std::cout << "list b = ";
        for (int b_temp = 0; b_temp < list_b_len; b_temp++) {
            int b_idx = b_temp + b_offset_temp;
            int b_1_dim = b_idx & 0xf;
            int b_2_dim = b_idx >> T_offset;
            std::cout << list_b[j][b_1_dim][b_2_dim] << " ";
        }
        std::cout << " b_len = " << list_b_len << " offset = " << list_b_offset << std::endl;

        // print list a two lines
        std::cout << "list a [" << j << "] = ";
        for (int debug_j = 0; debug_j < 2; debug_j++) {
            for (int debug_k = 0; debug_k < T; debug_k ++) {
                std::cout << list_a[j][debug_k][debug_j] << " ";
            }
        }
        std::cout << std::endl;

        // print list b two lines
        std::cout << "list b [" << j << "] = ";
        for (int debug_j = 0; debug_j < 2; debug_j++) {
            for (int debug_k = 0; debug_k < T; debug_k ++) {
                std::cout << list_b[j][debug_k][debug_j] << " ";
            }
        }
        std::cout << std::endl;

        std::cout << "tc_num = " << tc_num[0] << std::endl;
#endif
        temp_result[j] = tc_num[0];
    }

    process_result_sum: for (int j = 0; j < Parallel; j++) {
        tri_count += temp_result[j];
    }

    tc_count[0] = tri_count;
}


extern "C" {
void TriangleCount (int512* edge_list, int* offset_list_1, int* offset_list_2, \
                    int512* column_list_1, int512* column_list_2, int edge_num, int* tc_number ) {

#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 8 max_read_burst_length = 16 bundle = gmem0 port = edge_list
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 8 max_read_burst_length = 16 bundle = gmem1 port = offset_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 8 max_read_burst_length = 16 bundle = gmem2 port = offset_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 8 max_read_burst_length = 16 bundle = gmem3 port = column_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_read_outstanding = 8 max_read_burst_length = 16 bundle = gmem4 port = column_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 16 num_write_outstanding = 4 max_write_burst_length = 2 bundle = gmem5 port = tc_number

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
    static hls::stream<bool> ctrlStrm;
#pragma HLS STREAM variable = ctrlStrm depth=2
    static hls::stream<para_int> offLenStrmA;
#pragma HLS STREAM variable = offLenStrmA depth=512
    static hls::stream<para_int> offLenStrmB;
#pragma HLS STREAM variable = offLenStrmB depth=2
    static hls::stream<bool> ctrlStrm2;
#pragma HLS STREAM variable = ctrlStrm2 depth=2
    static hls::stream<req_int> reqStrmB;
#pragma HLS STREAM variable = reqStrmB depth=2

    int list_a_ping[Parallel][T][BUF_DEPTH];
    int list_b_ping[Parallel][T][BUF_DEPTH];
    int list_a_pong[Parallel][T][BUF_DEPTH];
    int list_b_pong[Parallel][T][BUF_DEPTH];
#pragma HLS array_partition variable=list_a_ping type=complete dim=1
#pragma HLS array_partition variable=list_a_ping type=complete dim=2
#pragma HLS array_partition variable=list_b_ping type=complete dim=1 
#pragma HLS array_partition variable=list_b_ping type=complete dim=2
#pragma HLS array_partition variable=list_a_pong type=complete dim=1
#pragma HLS array_partition variable=list_a_pong type=complete dim=2
#pragma HLS array_partition variable=list_b_pong type=complete dim=1 
#pragma HLS array_partition variable=list_b_pong type=complete dim=2


    int list_a_cache[1][T][BUF_DEPTH];
#pragma HLS array_partition variable=list_a_cache type=complete dim=1
#pragma HLS array_partition variable=list_a_cache type=complete dim=2

    int list_a_cache_tag[2];
    list_a_cache_tag[0] = -1; // invalid data in cache
    list_a_cache_tag[1] = 0; // valid data length

    int list_a_pong_tag[Parallel]; // indicate list_a value, if hit, no need to copy.
    int list_a_ping_tag[Parallel];
#pragma HLS array_partition variable=list_a_pong_tag type=complete dim=1
#pragma HLS array_partition variable=list_a_ping_tag type=complete dim=1
    for (int i = 0; i < Parallel; i++) {
        list_a_pong_tag[i] = -1;
        list_a_ping_tag[i] = -1;
    }

    int list_b_cache[Cache_size][T][BUF_DEPTH];
#pragma HLS array_partition variable=list_b_cache type=complete dim=1
#pragma HLS array_partition variable=list_b_cache type=complete dim=2

    int triCount_ping[1]={0};
    int triCount_pong[1]={0};
    int length = edge_num*2;

    loadEdgeList(length, edge_list, edgeStrm);
    loadOffset(length, offset_list_1, offset_list_2, edgeStrm, ctrlStrm, offLenStrmA, offLenStrmB);
    preLoadListB (offLenStrmB, ctrlStrm, ctrlStrm2, reqStrmB);

    int pp = 0; // ping-pong operation
    int TC_ping = 0;
    int TC_pong = 0;
    bool strm_control;


    // need to be parallel processed, complete partition
    para_int a_ele_out_ping[1];
    para_int a_ele_out_pong[1];
    para_int b_ele_out_ping[1];
    para_int b_ele_out_pong[1];

    pp_load_cpy_process: while(1) {

#pragma HLS pipeline

        strm_control = ctrlStrm2.read();
        para_int a_ele_in = offLenStrmA.read();
        req_int b_ele_in = reqStrmB.read();

        if (pp) {
            processList (list_a_ping, list_b_ping, a_ele_out_pong, b_ele_out_pong, triCount_pong);
            loadCpyListA (a_ele_in, column_list_1, list_a_cache, list_a_cache_tag, list_a_pong, list_a_pong_tag, a_ele_out_ping);
            loadListB (b_ele_in, column_list_2, list_b_cache, list_b_pong, b_ele_out_ping);
            TC_pong += triCount_pong[0];
        } else {
            processList (list_a_pong, list_b_pong, a_ele_out_ping, b_ele_out_ping, triCount_ping);
            loadCpyListA (a_ele_in, column_list_1, list_a_cache, list_a_cache_tag, list_a_ping, list_a_ping_tag, a_ele_out_pong);
            loadListB (b_ele_in, column_list_2, list_b_cache, list_b_ping, b_ele_out_pong);
            TC_ping += triCount_ping[0];
        }
        pp = 1 - pp;

        if (strm_control == false) {
            break;
        }
    }
    tc_number[0] = TC_ping + TC_pong;
}
}
