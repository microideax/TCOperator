#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <chrono>
#include "cmdlineparser.h"
#include <fstream>

// XRT includes
#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

#define PARTITION_NUM   1
#define USE_REORDER     false

bool fileExists(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();
}

int getPartitionSize(std::string dataset_name) {
    for (int i = 0; i < 10000; i++){
        std::string file_name = "./dataset_adj_test/" + dataset_name + "_col_" + std::to_string(i) + ".txt";
        if (fileExists(file_name) == false) {
            return i;
        }
    }
    return 0;
}

void getTxtSize (std::string file_name, int& lineNum) {
    // std::cout << "Getting file size ... " << file_name;
    std::fstream file;
    int line_num = 0;
    file.open(file_name, std::ios::in);
    if (file.is_open()) {
        std::string temp;
        while(getline(file, temp)) {
            line_num ++;
        }
    }
    file.close();
    lineNum = line_num;
    // std::cout << " " << lineNum << std::endl;
}

void getTxtContent (std::string file_name, int* array, int lineNum, bool is_edge) {
    // std::cout << "Getting file content ... " << file_name << std::endl;
    std::fstream file;
    file.open(file_name, std::ios::in);
    if (file.is_open()) {
        std::string tp, s_tp;
        int i = 0, line = 0;
        int data_t[2];
        while(getline(file, tp)) {
            if (is_edge == true) {
                std::stringstream ss(tp);
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

int main(int argc, char** argv) {

    sda::utils::CmdLineParser parser;
    parser.addSwitch("--xclbin_file", "-x", "input binary file string", "");
    parser.addSwitch("--dataset_name", "-d", "dataset name", "facebook_combined");
    parser.parse(argc, argv);

    // Read settings
    std::string binaryFile = parser.value("xclbin_file");
    std::string datasetName = parser.value("dataset_name");
    int device_index = 0;

    if (argc < 3) {
        parser.printHelp();
        return EXIT_FAILURE;
    }

    std::cout << "Open the device" << device_index << std::endl;
    auto device = xrt::device(device_index);
    std::cout << "Load the xclbin " << binaryFile << std::endl;
    auto uuid = device.load_xclbin(binaryFile);

    std::string name = "TriangleCount:{TriangleCount_1}";
    xrt::kernel tc_krnl = xrt::kernel(device, uuid, name);

    int partition_number = getPartitionSize(datasetName);
    std::cout << "load graph dataset " << datasetName << ", partition number = " << partition_number << std::endl;

    int TcNum = 0;
    double Kernel_exe_time = 0;

    for (int p_idx = 0; p_idx < partition_number; p_idx++) {
    // for (int p_idx = 162; p_idx < 163; p_idx++) {
	std::cout << "partition id = " << p_idx << std::endl;
        int edgeNum;
        std::string edgeName = "./dataset_adj_test/" + datasetName + "_edge.txt";
        // std::string edgeName = "./dataset_test/edge.txt";
        getTxtSize(edgeName, edgeNum);

        int columnNum, offsetNum;
        std::string offsetName = "./dataset_adj_test/" + datasetName + "_row_align_" + std::to_string(p_idx) + ".txt";
        std::string columnName = "./dataset_adj_test/" + datasetName + "_col_align_" + std::to_string(p_idx) + ".txt";
        // std::string offsetName = "./dataset_test/row_double.txt";
        // std::string columnName = "./dataset_test/col.txt";

        getTxtSize(offsetName, offsetNum);
        getTxtSize(columnName, columnNum);

        // std::cout << " allocate memory in device" << std::endl;

        int edge_size_bytes = edgeNum * 2 * sizeof(int);
        xrt::bo edgeBuffer = xrt::bo(device, edge_size_bytes, tc_krnl.group_id(0));
        int* edgeList = edgeBuffer.map<int*>();
        getTxtContent(edgeName, edgeList, edgeNum, true);

        xrt::bo columnBuffer = xrt::bo(device, (columnNum * sizeof(int)), tc_krnl.group_id(2));
        xrt::bo offsetBuffer = xrt::bo(device, (offsetNum * sizeof(int)), tc_krnl.group_id(1));
        int* columnList = columnBuffer.map<int*>();
        int* offsetList = offsetBuffer.map<int*>();
        getTxtContent(columnName, columnList, columnNum, false);
        getTxtContent(offsetName, offsetList, offsetNum, false);

        xrt::bo resultBuffer = xrt::bo(device, sizeof(int), tc_krnl.group_id(0));
        int* result = resultBuffer.map<int*>();
        result[0] = 0;

        // std::cout << "synchronize input buffer data to device global memory\n";
        auto start_pcie = std::chrono::steady_clock::now();
        columnBuffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        offsetBuffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        edgeBuffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        resultBuffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        auto end_pcie = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_data_transfer = end_pcie-start_pcie;
        // std::cout << "Partition id = " << p_idx << " PCIe time = " << elapsed_data_transfer.count() << std::endl;

        // std::cout << "Execution of the kernel\n";

        auto start = std::chrono::steady_clock::now();
        xrt::run krnl_run = tc_krnl(edgeBuffer, offsetBuffer, columnBuffer, columnBuffer, edgeNum, resultBuffer);
        krnl_run.wait();
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end-start;

        resultBuffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        TcNum += result[0];
        Kernel_exe_time += elapsed_seconds.count();

        std::cout << "Partition id = " << p_idx << " TC number = " << result[0] << " Time elapse " << elapsed_seconds.count() << std::endl;
    }

    std::cout << "Overall TC number = " << TcNum << std::endl;
    std::cout << "Overall kernel time = " << Kernel_exe_time << std::endl;
    return 0;
}
