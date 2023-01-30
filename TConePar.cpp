#ifndef __XTRA_GRAPH_TRIANGLE_COUNT_HPP_
#define __XTRA_GRAPH_TRIANGLE_COUNT_HPP_

#include "ap_int.h"
#include <string.h>
#include <stdio.h>

#ifndef __SYNTHESIS__
#include "iostream"
#endif

#define BUF_DEPTH 1024

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
    return count;
}


template <typename DT>
unsigned int TriangleCount(unsigned int edge_num, DT &edgelist, DT &offset, DT &col){

unsigned int triangle_count = 0;

// Initial local memory space of list_a and list_b
DT list_a[BUF_DEPTH];
DT list_b[BUF_DEPTH];

// use memcpy to copy the list_a and list_b from memory
    for(i = 0; i < edge_num; i+=2){
        unsigned int node_a = *(edgelist + i);
        unsigned int node_b = *(edgelist + i +1);
        unsigned int vertex_a_idx = offset[node_a];
        unsigned int vertex_b_idx = offset[node_b];
        unsigned int len_a = offset[node_a + 1] - vertex_a_idx;
        unsigned int len_b = offset[node_b + 1] - vertex_b_idx;

        memcpy(list_a, (const unsigned int*)&col[vertex_a_idx], len_a);
        memcpy(list_b, (const unsigned int*)&col[vertex_b_idx], len_b);


        // Process setintersection on lists with the len
        unsigned int temp_count = 0;
        temp_count = SetIntersection(list_a, list_b, len_a, len_b);
        triangle_count += temp_count;
    }
    return triangle_count;
}