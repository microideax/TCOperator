#include <stdint.h>
#include <hls_stream.h>


#define u512_t      ap_uint<512> 
#define BUF_DEPTH   65532
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
void burstCpyArray(DT* inArr, DT* dstArr, int offset, int length) {
    for (int i = 0; i < length; i++) {
#pragma HLS loop_tripcount min = 1000 max = 1000
// can not add ii = 1
        dstArr[i] = inArr[i + offset];
    }
}

void setIntersection(int *list_a, int *list_b, int len_a, int len_b, int offset_1, int offset_2,  int* tc_cnt)
{
    int count = 0;
    int idx_a = 0, idx_b = 0;
    while ((idx_a < len_a) && (idx_b < len_b))
    {
// #pragma HLS pipeline ii = 1
// #pragma HLS loop_tripcount min = 20 max = 20
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
//     int list_a[BUF_DEPTH];
//     int list_b[BUF_DEPTH];
// #pragma HLS RESOURCE variable=list_a core=XPM_MEMORY uram
// #pragma HLS RESOURCE variable=list_b core=XPM_MEMORY uram

    int triangle_count = 0;

// use memcpy to copy the list_a and list_b from memory
    for (int i = 0; i < edge_num; i+=1) {
        int node_a = edge_list[i*2];
        int node_b = edge_list[i*2 + 1];
        // cout<< "read nodes: "<< node_a <<", "<<node_b << endl;
        int vertex_a_idx = offset_list_1[node_a];
        int vertex_b_idx = offset_list_2[node_b];
        int len_a = offset_list_1[node_a + 1] - vertex_a_idx;
        int len_b = offset_list_2[node_b + 1] - vertex_b_idx;
        // cout<< "lens of lists: "<< len_a <<", "<< len_b << endl;
        
        if ((len_a != 0) && (len_b != 0)) {
            // burstCpyArray<int>(column_list_1, list_a, len_a, vertex_a_idx);
            // burstCpyArray<int>(column_list_2, list_b, len_b, vertex_b_idx);
        // adjListCpy(list_a, &column_list[vertex_a_idx], len_a);
        // adjListCpy(list_b, &column_list[vertex_b_idx], len_b);

        // Process setintersection on lists with the len
            int temp_count[1] = {0};
            setIntersection(column_list_1, column_list_2, len_a, len_b, vertex_a_idx, vertex_b_idx, temp_count);
            triangle_count += temp_count[0];
        }
    }
    tc_number[0] = triangle_count;
}

}