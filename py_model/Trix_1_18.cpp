#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <string.h>
#include <iostream>

#include <omp.h>

#include <algorithm>
#include <execution>
#include <sys/time.h>

using namespace std;

#define ARRAY_SIZE 1000
#define NUM_THREADS 1
#define NUM_PARTITION 4

static omp_lock_t lock[NUM_PARTITION];

bool compare(const vector<int> &a, const vector<int> &b){
    if(a[0] == b[0]){
        return a[1] < b[1];
    }
    else{
        return a[0] < b[0];
    }
}

void process_partition(int start, int end, int thread_id, int partition_index, vector<vector<int>> array, vector<vector<int>> partition_array[], int **csr_row) {
    //vector<int> tmp(0, 1);
    
    for (int index = start; index < end; index++) {
        // Process array elements within the partition
        
        int array_index = array[index][1] / partition_index;
        if(array_index >= NUM_PARTITION){
            array_index = NUM_PARTITION - 1;
        }
        //printf("This is %d thread, and put index %d into partition %d\n", thread_id, index, array_index);
        //printf("This is %d and end is %d\n", index, end);
        //omp_set_lock(&lock[array_index]);
        partition_array[array_index].push_back(array[index]);
        csr_row[array_index][array[index][0] + 1] += 1;
        //omp_unset_lock(&lock[array_index]);
        
    }
}

void generate_csr_data(int thread_id, vector<vector<int>> partition_array[], int **csr_row, int row_len){
    // np.add.accumulate 
    // gengerate csr_row
    sort(execution::par, partition_array[thread_id].begin(), partition_array[thread_id].end(), compare);
    for(int index = 1; index < row_len; index++){
        csr_row[thread_id][index] = csr_row[thread_id][index] + csr_row[thread_id][index - 1];
    }
}

int main(){
    FILE *file;
    char line[100];

    vector<vector<int>> edge_list;
    int values[2];
    const char delimiter[] = " ";

    file = fopen("./amazon0302_edge.txt", "r");
    if(file == NULL){
        perror("Error opening file");
        return 1;
    }
    int adj_matrix_dim = 0;
    while(fgets(line, sizeof(line), file) != NULL){
        char *token = strtok(line, delimiter);
        int index = 0;
        while (token != NULL) {
            values[index] = atoi(token);
            index++;
            token = strtok(NULL, delimiter);
        }
        if(values[1] > adj_matrix_dim){
            adj_matrix_dim = values[1];
        }
        vector<int> edge(values, values + 2); 
        edge_list.push_back(edge);  
    }
    fclose(file);
    cout << "Load edge list from file finished" << endl;

    int len_vector = edge_list.size();
    /*
    for(int i = 0; i < len_vector; i++){
        cout << edge_list[i][0] << " " << edge_list[i][1] << endl;
    }
    */
    double start, start1, end;
    start = omp_get_wtime();
    int edge_length = len_vector / NUM_THREADS;

    int partition_matrix_dim_num = adj_matrix_dim / NUM_PARTITION;

    //printf("The max index is %d and the partition index is %d\n", adj_matrix_dim, partition_matrix_dim_num);
    
    vector<vector<int>> tmp_list[NUM_PARTITION];

    for(int lock_num = 0; lock_num < NUM_PARTITION; lock_num++){
        omp_init_lock(&lock[lock_num]);
    }
    omp_set_num_threads(NUM_THREADS);
    
    int **csr_row;
    csr_row = (int**)malloc(NUM_PARTITION * sizeof(int*));
    
    for(int row = 0; row < NUM_PARTITION; row++){
        csr_row[row] = (int*)malloc((adj_matrix_dim + 1) * sizeof(int));
    }

    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        int partition_size = len_vector / NUM_THREADS;
        int start = thread_id * partition_size;
        int end = (thread_id == NUM_THREADS - 1) ? len_vector : start + partition_size;
        #pragma omp barrier
        {
            process_partition(start, end, thread_id, partition_matrix_dim_num, edge_list, tmp_list, csr_row);
        }

    }

    //for(int index = 0; index < NUM_PARTITION; index++){
    //            cout << tmp_list[index].size() << " ";
    //    }

    //cout << len_vector << endl;
    start1 = omp_get_wtime();
    omp_set_num_threads(4); 
    #pragma omp parallel
    {
        int thread_id = omp_get_thread_num();
        generate_csr_data(thread_id, tmp_list, csr_row, adj_matrix_dim + 1);

    }
    end = omp_get_wtime();


    

    printf("The time use is %lfs %lfs and the total cost is %lfs\n", start1 - start, end - start1, end - start);


    // write csr result to files
    char file_name1[100], file_name2[100];
    for(int index = 0; index < NUM_PARTITION; index ++){
        sprintf(file_name1, "../datasets/partition_dataset/%s_col_%d.txt", "amazon0302", index);
        sprintf(file_name2, "../datasets/partition_dataset/%s_row_%d.txt", "amazon0302", index);
        FILE *fp1 = fopen(file_name1, "w+");
        FILE *fp2 = fopen(file_name2, "w+");
        //printf("%d\n", tmp_list[index].size());
        for(int num_col = 0; num_col < tmp_list[index].size(); num_col++){
            //printf("%d\n", csr_row[index][num_col]);
            fprintf(fp1, "%d\n", tmp_list[index][num_col][1]);
            fprintf(fp2, "%d\n", csr_row[index][num_col]);
        }
        fclose(fp1);
        fclose(fp2);
    }

    for(int row = 0; row < NUM_PARTITION; row++){
        free(csr_row[row]);
    }
    free(csr_row);

    return 0;
}