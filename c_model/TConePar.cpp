
#ifndef __XTRA_GRAPH_TRIANGLE_COUNT_HPP_
#define __XTRA_GRAPH_TRIANGLE_COUNT_HPP_
#endif

// #include "ap_int.h"
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdio.h>

#ifndef __SYNTHESIS__
#include "iostream"
#endif

#define BUF_DEPTH 65536
#define E 20
#define V 10

using namespace std;


template <typename DT>
void adjListCpy(DT* dst, DT* start_location, unsigned int len){
    // cout<<"copy list: ";
    for (unsigned int i=0; i<len; i++){
        *(dst+i) = *(start_location + i);
        // cout<<*(dst+i) << " ";
    }
    // cout<<endl;
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
    // cout<<"counted triangle: "<< count<< endl;
    return count;
}


template <typename DT>
int binarySearch(DT arr[], int low, int high, DT key){
    int mid = 0;
    while (low <= high){
        mid = low + (high - low)/2;
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

template <typename DT>
unsigned int setInterBinarySearch(DT *list_a, DT *list_b, int len_a, int len_b){
/** Need to always make sure the list_a is the shorter one **/
    unsigned int count = 0;
    int temp_idx = 0;
    // list_a has to be the shorter one, search with element from shorter array
    for (int i = 0; i < len_a; i++){
        temp_idx = binarySearch(list_b, 0, len_b-1, list_a[i]);
        if (temp_idx != -1) {
            count++;
        }
    }
    return count;
}


template <typename DT>
unsigned int TriangleCount(unsigned int edge_num, DT* edge_list, DT* offset, DT* col){

unsigned int triangle_count = 0;

// Initial local memory space of list_a and list_b
DT list_a[BUF_DEPTH];
DT list_b[BUF_DEPTH];

// use memcpy to copy the list_a and list_b from memory
    for(unsigned int i = 0; i < edge_num; i+=1) {
        unsigned int node_a = *(edge_list + i*2);
        unsigned int node_b = *(edge_list + i*2 + 1);
        // cout<< "read nodes: "<< node_a <<", "<<node_b << endl;
        unsigned int vertex_a_idx = offset[node_a];
        unsigned int vertex_b_idx = offset[node_b];
        unsigned int len_a = offset[node_a + 1] - vertex_a_idx;
        unsigned int len_b = offset[node_b + 1] - vertex_b_idx;
        // cout<< "lens of lists: "<< len_a <<", "<< len_b << endl;
        
        if(len_a != 0 && len_b != 0){
        adjListCpy(list_a, &col[vertex_a_idx], len_a);
        adjListCpy(list_b, &col[vertex_b_idx], len_b);

        // Process setintersection on lists with the len
        unsigned int temp_count = 0;
        /** Using normal set intersection function
        temp_count = SetIntersection<DT>(list_a, list_b, len_a, len_b);
        **/

        /** Using binary search based set intersection function **/
        if (len_a <= len_b){
            temp_count = setInterBinarySearch<DT>(list_a, list_b, len_a, len_b);
        } else {
            temp_count = setInterBinarySearch<DT>(list_b, list_a, len_b, len_a);
        }

        triangle_count += temp_count;
        }
    }
    return triangle_count;
}

void getTxtSize (string file_name, int& lineNum) {
    cout << "Getting file size ... " << file_name << endl;
    fstream file;
    int line_num = 0;
    file.open(file_name, std::ios::in);
    if (file.is_open()) {
        string temp;
        while(getline(file, temp)) {
            line_num ++;
        }
    }
    file.close();
    lineNum = line_num;
}

void getTxtContent (string file_name, int* array, int lineNum, bool is_edge) {
    cout << "Getting file content ... " << file_name << endl;
    fstream file;
    file.open(file_name, std::ios::in);
    if (file.is_open()) {
        string tp, s_tp;
        int i = 0, line = 0;
        int data_t[2];
        while(getline(file, tp)) {
            if (is_edge == true) {
                stringstream ss(tp);
                while (getline(ss, s_tp, ' ')) {
                    data_t[i] = stoi(s_tp);
                    i = (i + 1) % 2;
                }
                array[line*2] = data_t[0];
                array[line*2 + 1] = data_t[1];
                line++;
            } else {
                array[line] = stoi(tp);
                line++;
            }
        }
        file.close(); // close the file object.
        if (line != lineNum) { // double check
            std::cout << "[ERROR] read file not align " << std::endl;
        }
    } else {
        std::cout << "[ERROR] can not open file" << std::endl;
    }
}


int main(){

    cout<< "--------Triangle Counting c_model--------" << endl;

/*  for naive test

    unsigned int edgelist[E*2] = {0,1,0,2,0,3,0,4,0,5,1,2,2,3,2,4,4,5};
    unsigned int csr_offset[V] = {0,5,6,8,8,9,9};
    unsigned int csr_columns[E] = {1,2,3,4,5,2,3,4,5};
*/

    int PARTITION_NUM = 8;
    string datasetName = "facebook_combined";
    cout << " Read edge file ... " << endl;

    fstream edge_file;
    string edgeName = "./dataset/" + datasetName + "_edge.txt";
    int edgeNum = 0;
    getTxtSize(edgeName, edgeNum);
    int * edgeList = new int [edgeNum * 2];
    getTxtContent(edgeName, edgeList, edgeNum, true);

    unsigned int tcNum = 0;
    for (int i = 0; i < PARTITION_NUM; i++) {
        string rowName = "./dataset/" + datasetName + "_row_" + to_string(i) + ".txt";
        string colName = "./dataset/" + datasetName + "_col_" + to_string(i) + ".txt";
        int rowNum = 0, colNum = 0;
        getTxtSize(rowName, rowNum);
        getTxtSize(colName, colNum);
        int* rowList = new int [rowNum];
        int* colList = new int [colNum];
        getTxtContent(rowName, rowList, rowNum, false);
        getTxtContent(colName, colList, colNum, false);

        tcNum += TriangleCount<int>(edgeNum, edgeList, rowList, colList);
    }

    cout<< "counted triangle numer: " << tcNum << endl;
    return 1;
}