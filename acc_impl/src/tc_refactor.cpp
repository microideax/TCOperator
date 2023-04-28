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
#define len_coale   512

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
#pragma HLS pipeline //This pipeline pragma seems unnecessary, consider to remove
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

void preProcess (int length, hls::stream<int512>& edgeInput,
                 hls::stream<int256>& offsetInStrmA, hls::stream<int256>& offsetInStrmB,
                 hls::stream<int256>& lengthInStrmA, hls::stream<int256>& lengthInStrmB,
                 hls::stream<int256>& offsetOutStrmA, hls::stream<int256>& offsetOutStrmB,
                 hls::stream<int256>& lengthOutStrmA, hls::stream<int256>& lengthOutStrmB,
                 hls::stream<int256>& flagStrmA, hls::stream<int256>& flagStrmB) {
    // preProcess Stage:
    // For list A -> hit miss check and filter len = 1;
    // For list B -> coalescing and filter len = 1;
    // define List A flags: 0 -> no load, 1 -> copy, 2 -> load.
    // define List B flags: 0 -> no load, 1 -> coalescing, 2 -> coalescing end, 3 -> load.

    int512 edge_value;
#pragma HLS array_partition variable=edge_value type=complete dim=1
    int256 length_a, offset_a, length_b, offset_b;
#pragma HLS array_partition variable=length_a type=complete dim=1
#pragma HLS array_partition variable=offset_a type=complete dim=1
#pragma HLS array_partition variable=length_b type=complete dim=1
#pragma HLS array_partition variable=offset_b type=complete dim=1
    int256 flag_list_a, flag_list_b;
#pragma HLS array_partition variable=flag_list_a type=complete dim=1
#pragma HLS array_partition variable=flag_list_b type=complete dim=1

    // one unified cache for list a ping pong buffer.
    int list_a_cache_tag = -1; // value
    // ping and pong tag for separate ping and pong buffer. 0 for pong, 1 for ping.
    int list_a_tag[2][T_2];
#pragma HLS array_partition variable=list_a_tag type=complete dim=1
#pragma HLS array_partition variable=list_a_tag type=complete dim=2

    // list b coalescing item and index array
    int item_num = 0;
    int coalesce_index[T_2];
    int temp_offset = -1;
    int temp_length = 0;

    for (int i = 0; i < 2; i++) {
#pragma HLS unroll
        for (int j = 0; j < T_2; j++) {
#pragma HLS unroll
            // initial
            list_a_tag[i][j] = -1;
            coalesce_index[j] = -1;
        }
    }

    int loop = (length + T - 1) / T;
    int pp = 0;
    bool ignore = false; // when len_a < 2, ignore = true;

    for (int i = 0; i <= loop; i++) {
        edge_value = edgeInput.read();
        length_a = lengthInStrmA.read();
        offset_a = offsetInStrmA.read();
        length_b = lengthInStrmB.read();
        offset_b = offsetInStrmB.read();

        for (int j = 0; j < T_2; j++) {
#pragma HLS pipeline
            // preprocess for list a:
            // if (length_a.data[j] == 1) {
            //     // list a length = 1, do not need TC.
            //     ignore = true; // should be true
            //     flag_list_a.data[j] = 0;
            // } else 
            
            if (edge_value.data[j*2] == list_a_tag[pp][j]) {
                // no need to copy or load
                flag_list_a.data[j] = 0;
            } else if (edge_value.data[j*2] == list_a_cache_tag) {
                // data in cache, hit, need copy
                list_a_tag[pp][j] = edge_value.data[j*2];
                flag_list_a.data[j] = 1;
            } else {
                // data miss, need load from off-chip memory
                list_a_tag[pp][j] = edge_value.data[j*2];
                list_a_cache_tag = edge_value.data[j*2];
                flag_list_a.data[j] = 2;
            }

            // preprocess for list b
            flag_list_b.data[j] = 3; // default, load

            if (j < T_2 - 1) {
                temp_offset = offset_b.data[j] + length_b.data[j] - offset_b.data[j + 1];
                if ((temp_offset == 0) && 
                    ((temp_length + length_b.data[j]) < (65536 - 32))) {
                    // coalescing
                    item_num += 1;
                    temp_length += length_b.data[j];
                    flag_list_b.data[j] = 1;
                    continue;
                }
            }

            // if ((item_num == 0) && (ignore == true)) {
            //     // no coalescing and ignore == true
            //     flag_list_b.data[j] = 0; // no load
            //     ignore = false;
            //     continue;
            // }

            if (item_num > 0) {
                flag_list_b.data[j] = 2; // coalescing end.
                temp_length = 0;
                item_num = 0;
                continue;
            }
        }
        item_num = 0;
        pp = 1 - pp;
        offsetOutStrmA << offset_a;
        offsetOutStrmB << offset_b;
        lengthOutStrmA << length_a;
        lengthOutStrmB << length_b;
        flagStrmA << flag_list_a;
        flagStrmB << flag_list_b;
    }
}

void loadCpyListA ( int512* column_list_1, int256 flagStrmA_value,
                    int256 offsetStrmA_value, int256 lengthStrmA_value, 
                    int list_a_cache[1][T][BUF_DEPTH], int list_a[T_2][T][BUF_DEPTH],
                    int256 offsetValueA[1], int256 lengthValueA[1]) {

    int256 offset_value, length_value, flag_value;
#pragma HLS array_partition variable=offset_value type=complete dim=1
#pragma HLS array_partition variable=length_value type=complete dim=1
#pragma HLS array_partition variable=flag_value type=complete dim=1

    offset_value = offsetStrmA_value;
    length_value = lengthStrmA_value;
    flag_value = flagStrmA_value;

    load_copy_list_a_T_2: for (int j = 0; j < T_2; j++) {
    if (flag_value.data[j] == 0) {
        // no load
    } else if (flag_value.data[j] == 1) {
        // hit, copy data from list_a cache
        // std::cout << "Copy list a: " << list_cache_tag << std::endl;
        int o_begin_a = offset_value.data[j] >> T_offset;
        int o_end_a = (offset_value.data[j] + length_value.data[j] + T - 1) >> T_offset;
        copy_list_a: for (int ii = 0; ii < (o_end_a - o_begin_a); ii++) {
#pragma HLS pipeline
            for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                list_a[j][jj][ii] = list_a_cache[0][jj][ii];
            }
        }
    } else if (flag_value.data[j] == 2) {
        // miss, load data from off-chip memory and update cache
        int o_begin_a = offset_value.data[j] >> T_offset;
        int o_end_a = (offset_value.data[j] + length_value.data[j] + T - 1) >> T_offset;
        // std::cout << "Load list a: " << offset_value.data[j] << std::endl;
        load_list_a: for (int ii = o_begin_a; ii < o_end_a; ii++) {
#pragma HLS pipeline
            int512 list_a_temp = column_list_1[ii];
            for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                list_a[j][jj][ii - o_begin_a] = list_a_temp.data[jj];
                list_a_cache[0][jj][ii - o_begin_a] = list_a_temp.data[jj];
            }
        }
    }
    }

    offsetValueA[0] = offset_value;
    lengthValueA[0] = length_value;
}

void loadListB (int512* column_list_2, int256 flagStrmB_value, 
                int256 offsetStrmB_value, int256 lengthStrmB_value,
                int list_b[T_2][T][BUF_DEPTH],
                int256 offsetValueB[1], int256 lengthValueB[1]) {

    int256 offset_value, length_value, flag_value;
#pragma HLS array_partition variable=offset_value type=complete dim=1
#pragma HLS array_partition variable=length_value type=complete dim=1
#pragma HLS array_partition variable=flag_value type=complete dim=1

    offset_value = offsetStrmB_value;
    length_value = lengthStrmB_value;
    flag_value = flagStrmB_value;

    int item_num = 0;
    int o_begin_b, o_end_b, start_pos, end_pos;
    int coalesce_index[T_2];

    int list_buffer[T][BUF_DEPTH];
#pragma HLS array_partition variable=list_buffer type=complete dim=1

    // define List B flags: 0 -> no load, 1 -> coalescing, 2 -> coalescing end, 3 -> load.
    load_list_b_T_2: for (int j = 0; j < T_2; j++) {
        if (flag_value.data[j] == 0) {
            // no load
        } else if (flag_value.data[j] == 1) {
            // coalescing 
            coalesce_index[item_num] = j;
            item_num ++;
        } else if (flag_value.data[j] == 2) {
            coalesce_index[item_num] = j;
            start_pos = offset_value.data[coalesce_index[0]];
            end_pos = offset_value.data[j] + length_value.data[j];
            o_begin_b = start_pos >> T_offset;
            o_end_b = (end_pos + T - 1) >> T_offset;

            load_list_buffer: for (int ii = o_begin_b; ii < o_end_b; ii++) {
#pragma HLS pipeline
                int512 list_b_temp = column_list_2[ii];
                for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                    list_buffer[jj][ii - o_begin_b] = list_b_temp.data[jj];
                }
            }

            copy_buffer_to_list_b: for (int k = 0; k <= item_num; k++) {
            // copy buffer data to list_b
                int item_start_pos = (offset_value.data[coalesce_index[k]] >> T_offset) - o_begin_b;
                int item_end_pos = ((offset_value.data[coalesce_index[k]] + length_value.data[coalesce_index[k]] + T - 1) >> T_offset) - o_begin_b;
                int item_index = coalesce_index[k];
                // std::cout << "copy to list_b " << item_start_pos << " " << item_end_pos << " " << item_index << std::endl;
                for (int it = item_start_pos; it < item_end_pos; it++) {
#pragma HLS pipeline
                    for (int jj = 0; jj < T; jj++) {
#pragma HLS unroll
                        list_b[item_index][jj][it - item_start_pos] = list_buffer[jj][it];
                    }
                }
            }
            item_num = 0; // clean the item index
        } else if (flag_value.data[j] == 3) {
            // no need to use buffer
            int start_pos = offset_value.data[j];
            int end_pos = offset_value.data[j] + length_value.data[j];
            int o_begin_b = start_pos >> T_offset;
            int o_end_b = (end_pos + T - 1) >> T_offset;
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

    offsetValueB[0] = offset_value;
    lengthValueB[0] = length_value;
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
#pragma HLS pipeline ii=1
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

void processList(int list_a[T_2][T][BUF_DEPTH], int list_b[T_2][T][BUF_DEPTH], 
                 int256 offsetValueA[1], int256 offsetValueB[1],
                 int256 lengthValueA[1], int256 lengthValueB[1], int* tc_count) {
#pragma HLS inline off
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
    static hls::stream<int256> offsetOutStrm_A;
#pragma HLS STREAM variable = offsetOutStrm_A depth=16
    static hls::stream<int256> offsetOutStrm_B;
#pragma HLS STREAM variable = offsetOutStrm_B depth=16
    static hls::stream<int256> lengthOutStrm_A;
#pragma HLS STREAM variable = lengthOutStrm_A depth=16
    static hls::stream<int256> lengthOutStrm_B;
#pragma HLS STREAM variable = lengthOutStrm_B depth=16
    static hls::stream<int256> flagStrm_A;
#pragma HLS STREAM variable = flagStrm_A depth=16
    static hls::stream<int256> flagStrm_B;
#pragma HLS STREAM variable = flagStrm_B depth=16

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

    int triCount_ping[1]={0};
    int triCount_pong[1]={0};
    int length = edge_num*2;

    loadEdgeList(length, edge_list, edgeStrm);
    loadOffset(length, offset_list_1, offset_list_2, edgeStrm, edgeStrmOut, \
                offsetStrm_A, offsetStrm_B, lengthStrm_A, lengthStrm_B);
    preProcess(length, edgeStrmOut, offsetStrm_A, offsetStrm_B, lengthStrm_A, lengthStrm_B,
                offsetOutStrm_A, offsetOutStrm_B, lengthOutStrm_A, lengthOutStrm_B, flagStrm_A, flagStrm_B);

    int loop = (length + T - 1) /T;
    int pp = 0; // ping-pong operation
    int TC_ping = 0;
    int TC_pong = 0;

    int256 offset_strm_a, offset_strm_b;
    int256 length_strm_a, length_strm_b;
    int256 flag_strm_a, flag_strm_b;

    pp_load_cpy_process: for (int i = 0; i <= loop; i++) {
        offset_strm_a = offsetOutStrm_A.read();
        offset_strm_b = offsetOutStrm_B.read();
        length_strm_a = lengthOutStrm_A.read();
        length_strm_b = lengthOutStrm_B.read();
        flag_strm_a = flagStrm_A.read();
        flag_strm_b = flagStrm_B.read();

        if (pp) {
            processList (list_a_ping, list_b_ping, offset_a_ping, offset_b_ping, \
                         length_a_ping, length_b_ping, triCount_pong);
            loadCpyListA (column_list_1, flag_strm_a, offset_strm_a, length_strm_a, 
                          list_a_cache, list_a_pong, offset_a_pong, length_a_pong);
            loadListB (column_list_2, flag_strm_b, offset_strm_b, length_strm_b, 
                          list_b_pong, offset_b_pong, length_b_pong);
            TC_pong += triCount_pong[0];
        } else {
            processList (list_a_pong, list_b_pong, offset_a_pong, offset_b_pong, \
                         length_a_pong, length_b_pong, triCount_ping);
            loadCpyListA (column_list_1, flag_strm_a, offset_strm_a, length_strm_a, 
                          list_a_cache, list_a_ping, offset_a_ping, length_a_ping);
            loadListB (column_list_2, flag_strm_b, offset_strm_b, length_strm_b, 
                          list_b_ping, offset_b_ping, length_b_ping);
            TC_ping += triCount_ping[0];
        }
        pp = 1 - pp;
    }
    tc_number[0] = TC_ping + TC_pong;
}
}
