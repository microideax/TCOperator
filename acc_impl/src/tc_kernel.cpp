#include <stdint.h>
#include <hls_stream.h>
#include <iostream>


#define u512_t      ap_uint<512> 
#define BUF_DEPTH   65536
// use 512 bit width to full utilize bandwidth

template <typename DT>
void burstReadStrm(int length, DT* inArr, hls::stream<DT>& outStrm) {
    for (int i = 0; i < length; i++) {
#pragma HLS loop_tripcount min = 1000 max = 1000
#pragma HLS pipeline ii = 1
        outStrm.write(inArr[i]);
    }
}

template <typename DT>
void adjListCpy(DT* dst, DT* start_location, unsigned int len){
    // cout<<"copy list: ";
    for (unsigned int i=0; i<len; i++){
//        for(unsigned int j = 0; j < 8; j++)
//#pragma HLS pipeline ii = 1
        *(dst+i) = *(start_location + i);
        // cout<<*(dst+i) << " ";
    }
    // cout<<endl;
}

template <typename DT>
void burstCpyArray(DT* inPort, DT* dstArr, int offset, int length) {
    for (int i = 0; i < length; i++) {
#pragma HLS loop_tripcount min = 1000 max = 1000
// can not add ii = 1
        dstArr[i] = inPort[i + offset];
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

int highestPowerof2(int n){
    int res = 0;
    if(n == 1){
        res = 1;
    } else if (n >= 2) {
        res = 1;
        while (res*2 <= n) {
            res = res * 2;
        }
    }
    return res * 2;
}

template <typename DT>
int lbitBinarySearch(DT arr[], int low, int high, DT key){
    int k = highestPowerof2(high)/2;
    // int k = (high+1)/2 + 1;
    // int k = (high+1)>>1;
    // if (high >= 16) {
    //     k = 16;
    // } else {
    //     k = high;
    // }
    int r;
    int i = (arr[k] <= key) ? k : 0;
    while ((k >>= 1)){
#pragma HLS pipeline ii=1
        r = i + k;
        if ((r <= high) && (key >= arr[r])) {
            i = r;
        }
    }
    if (arr[i] == key) {
        return i;
    } else {
        return -1;
    }
}

template <typename DT>
void setInterBinarySearch(DT *list_a, DT *list_b, int len_a, int len_b, int* tc_cnt){
/** Need to make sure the list_a is the shorter one **/
    unsigned int count = 0;
    int temp_idx_0 = 0;
    // list_a has to be the shorter one, search with element from shorter array
    setint: for (int i = 0; i < len_a; i++){
        temp_idx_0 = lbitBinarySearch<DT>(list_b, 0, len_b-1, list_a[i]);
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

void setIntersection(int *list_a, int *list_b, int len_a, int len_b, int offset_1, int offset_2,  int* tc_cnt)
{
    int count = 0;
    int idx_a = 0, idx_b = 0;
    while ((idx_a < len_a) && (idx_b < len_b))
    {
#pragma HLS pipeline ii = 1
#pragma HLS loop_tripcount min = 20 max = 20
        if(list_a[idx_a + offset_1] < list_b[idx_b + offset_2])
            idx_a++;
        else if (list_a[idx_a + offset_1] > list_b[idx_b + offset_2])
            idx_b++;
        else {
            count++;
            idx_a++;
            idx_b++;
        }
    }
    tc_cnt[0] = count;
}

void setIntersection(int *list_a, int *list_b, int len_a, int len_b, int* tc_cnt)
{
    int count = 0;
    int idx_a = 0, idx_b = 0;
    while ((idx_a < len_a) && (idx_b < len_b))
    {
#pragma HLS pipeline ii = 1
#pragma HLS loop_tripcount min = 20 max = 20
        if(list_a[idx_a] < list_b[idx_b])
            idx_a++;
        else if (list_a[idx_a] > list_b[idx_b])
            idx_b++;
        else {
            count++;
            idx_a++;
            idx_b++;
        }
    }
    tc_cnt[0] = count;
}

void setIntersectionForloop(int *list_a, int *list_b, int len_a, int len_b, int offset_1, int offset_2,  int* tc_cnt)
{
    int count = 0;
    for (int idx_a = 0, idx_b = 0; idx_a < len_a && idx_b < len_b;)
    {
        if(list_a[idx_a + offset_1] < list_b[idx_b + offset_2])
            idx_a++;
        else if (list_a[idx_a + offset_1] > list_b[idx_b + offset_2])
            idx_b++;
        else {
            count++;
            idx_a++;
            idx_b++;
        }
    }
    tc_cnt[0] = count;
}


extern "C" {

void TriangleCount(int* edge_list, int* offset_list_1, int* offset_list_2, int* column_list_1, int* column_list_2, int edge_num, int* tc_number ) {

#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    16 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem0 port = edge_list
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    16 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem1 port = offset_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    16 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem2 port = offset_list_2
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    16 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem3 port = column_list_1
#pragma HLS INTERFACE m_axi offset = slave latency = 64 num_write_outstanding = 32 num_read_outstanding = \
    16 max_write_burst_length = 2 max_read_burst_length = 64 bundle = gmem4 port = column_list_2
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
    int list_a[BUF_DEPTH];
    int list_b[BUF_DEPTH];
// #pragma HLS RESOURCE variable=list_a core=XPM_MEMORY uram
// #pragma HLS RESOURCE variable=list_b core=XPM_MEMORY uram

    int triangle_count = 0;
    int previous_node_a = -1;
// use memcpy to copy the list_a and list_b from memory
    for (int i = 0; i < edge_num; i+=1) {
//#pragma HLS DATAFLOW
        int node_a = edge_list[i*2];
        int node_b = edge_list[i*2 + 1];
        // std::cout<< "read nodes: "<< node_a <<", "<<node_b << std::endl;
        int vertex_a_idx = offset_list_1[node_a];
        int vertex_b_idx = offset_list_2[node_b];
        int len_a = offset_list_1[node_a + 1] - vertex_a_idx;
        int len_b = offset_list_2[node_b + 1] - vertex_b_idx;
        // std::cout<< "lens of lists: "<< len_a <<", "<< len_b << std::endl;
        
        if ((len_a != 0) && (len_b != 0)) {
            // burstCpyArray<int>(column_list_1, list_a, len_a, vertex_a_idx);
            // burstCpyArray<int>(column_list_2, list_b, len_b, vertex_b_idx);
            if (previous_node_a != node_a){
                adjListCpy<int>(list_a, &column_list_1[vertex_a_idx], len_a);   
                previous_node_a = node_a; 
            }
            adjListCpy<int>(list_b, &column_list_2[vertex_b_idx], len_b);

            // Process setintersection on lists with the lens are not zeros
            int temp_count[1] = {0};
            // setIntersection(column_list_1, column_list_2, len_a, len_b, vertex_a_idx, vertex_b_idx, temp_count);
            // setIntersection(list_a, list_b, len_a, len_b, temp_count);
            /**/
            if (len_a <= len_b){
                setInterBinarySearch<int>(list_a, list_b, len_a, len_b, temp_count);
            } else {
                setInterBinarySearch<int>(list_b, list_a, len_b, len_a, temp_count);
            }
            /**/
            triangle_count += temp_count[0];
        }
        // previous_node_a = node_a;
    }
    tc_number[0] = triangle_count;
}
}
