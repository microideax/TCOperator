
#ifndef __XTRA_GRAPH_TRIANGLE_COUNT_HPP_
#define __XTRA_GRAPH_TRIANGLE_COUNT_HPP_
#endif

// #include "ap_int.h"
// #include <string.h>
#include <stdio.h>

#ifndef __SYNTHESIS__
#include "iostream"
#endif

#define BUF_DEPTH 16
#define E 20
#define V 10

using namespace std;


template <typename DT>
void adj_list_cpy(DT* dst, DT* start_location, unsigned int len){
    cout<<"copy list: ";
    for (unsigned int i=0; i<len; i++){
        *(dst+i) = *(start_location + i);
        cout<<*(dst+i) << " ";
    }
    cout<<endl;
}

template <typename DT>
unsigned int SetIntersection(DT *list_a, DT *list_b, int len_a, int len_b)
{
    unsigned int count = 0;
    int idx_a = 0, idx_b = 0;
    while (idx_a < len_a && idx_b < len_b)
    {
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
    cout<<"counted triangle: "<< count<< endl;
    return count;
}


template <typename DT>
unsigned int TriangleCount(unsigned int edge_num, DT* edgelist, DT* offset, DT* col){

unsigned int triangle_count = 0;

// Initial local memory space of list_a and list_b
DT list_a[BUF_DEPTH];
DT list_b[BUF_DEPTH];

// use memcpy to copy the list_a and list_b from memory
    for(unsigned int i = 0; i < edge_num; i+=2){
        unsigned int node_a = *(edgelist + i);
        unsigned int node_b = *(edgelist + i +1);
        cout<< "read nodes: "<< node_a <<", "<<node_b << endl;
        unsigned int vertex_a_idx = offset[node_a];
        unsigned int vertex_b_idx = offset[node_b];
        unsigned int len_a = offset[node_a + 1] - vertex_a_idx;
        unsigned int len_b = offset[node_b + 1] - vertex_b_idx;
        cout<< "lens of lists: "<< len_a <<", "<< len_b << endl;
        
        if(len_a != 0 && len_b != 0){
        adj_list_cpy(list_a, &col[vertex_a_idx], len_a);
        adj_list_cpy(list_b, &col[vertex_b_idx], len_b);

        // Process setintersection on lists with the len
        unsigned int temp_count = 0;
        temp_count = SetIntersection<DT>(list_a, list_b, len_a, len_b);
        triangle_count += temp_count;
        }
    }
    return triangle_count;
}


int main(){

    cout<< "--------Triangle Counting c_model--------" << endl;

    unsigned int edgelist[E*2] = {0,1,0,2,0,3,0,4,0,5,1,2,2,3,2,4,4,5};
    unsigned int csr_offset[V] = {0,5,6,8,8,9,9};
    unsigned int csr_columns[E] = {1,2,3,4,5,2,3,4,5};
    cout<< "---------Initialized testing data-----------"<< endl;
    unsigned int tc_num=0;

    tc_num = TriangleCount<unsigned int>(8, edgelist, csr_offset, csr_columns);
    
    cout<< "counted triangle numer: " << tc_num << endl;
    return 1;
}