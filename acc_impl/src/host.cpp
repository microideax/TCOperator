#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <chrono>
#include "cmdlineparser.h"

// XRT includes
#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

#define PARTITION_NUM   4
#define USE_REORDER     false
#define COALESCE_DIST   16

void getTxtSize (std::string file_name, int& lineNum) {
    std::cout << "Getting file size ... " << file_name;
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
    std::cout << " " << lineNum << std::endl;
}

void getTxtContent (std::string file_name, int* array, int lineNum, bool is_edge) {
    std::cout << "Getting file content ... " << file_name << std::endl;
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

    std::string name;
    std::vector<xrt::kernel> tc_krnl;
    tc_krnl.resize(PARTITION_NUM);
    for (int i = 0; i < PARTITION_NUM; i++) {
        name = "TriangleCount:{TriangleCount_" + std::to_string(i+1) + "}";
       tc_krnl[i] = xrt::kernel(device, uuid, name);
    }

    std::cout << "load graph dataset" << std::endl;
    std::cout << "Read edge file ... " << std::endl;
    std::vector<std::string> edgeName;
    std::vector<int> edgeNum;
    edgeName.resize(PARTITION_NUM);
    edgeNum.resize(PARTITION_NUM);
    for (int i = 0; i < PARTITION_NUM; i++) {
#if USE_REORDER==true
        edgeName[i] = "./dataset_" + std::to_string(PARTITION_NUM) + "pe/" + datasetName + "_edge_reorder_" + std::to_string(i) + ".txt";
#else
        edgeName[i] = "./dataset_" + std::to_string(PARTITION_NUM) + "pe/" + datasetName + "_edge_" + std::to_string(i) + ".txt";
#endif
        getTxtSize(edgeName[i], edgeNum[i]);
    }

    int columnNum, offsetNum;
    name = "./dataset_" + std::to_string(PARTITION_NUM) + "pe/" + datasetName + "_row_double.txt";
    // name = "./dataset_" + std::to_string(PARTITION_NUM) + "pe/" + datasetName + "_row.txt";
    getTxtSize(name, offsetNum);
    name = "./dataset_" + std::to_string(PARTITION_NUM) + "pe/" + datasetName + "_col.txt";
    getTxtSize(name, columnNum);

    std::cout << " allocate memory in device" << std::endl;
    std::vector<xrt::bo> edgeBuffer;
    std::vector<int*> edgeList;
    edgeBuffer.resize(PARTITION_NUM);
    edgeList.resize(PARTITION_NUM);
    for (int i = 0; i < PARTITION_NUM; i++) {
        int edge_size_bytes = edgeNum[i] * 2 * sizeof(int);
        edgeBuffer[i] = xrt::bo(device, edge_size_bytes, tc_krnl[i].group_id(0));
        edgeList[i] = edgeBuffer[i].map<int*>();
        getTxtContent(edgeName[i], edgeList[i], edgeNum[i], true);
    }

    std::vector<xrt::bo> columnBuffer;
    std::vector<xrt::bo> offsetBuffer;
    std::vector<int*> columnList;
    std::vector<int*> offsetList;
    columnBuffer.resize(PARTITION_NUM);
    offsetBuffer.resize(PARTITION_NUM);
    columnList.resize(PARTITION_NUM);
    offsetList.resize(PARTITION_NUM);
    for (int i = 0; i < PARTITION_NUM; i++) {
        int size_bytes = columnNum * sizeof(int);
        columnBuffer[i] = xrt::bo(device, size_bytes, tc_krnl[i].group_id(3));
        columnList[i] = columnBuffer[i].map<int*>();
        getTxtContent("./dataset_" + std::to_string(PARTITION_NUM) + "pe/" + datasetName + "_col.txt", columnList[i], columnNum, false);

        size_bytes = offsetNum * sizeof(int);
        offsetBuffer[i] = xrt::bo(device, size_bytes, tc_krnl[i].group_id(1));
        offsetList[i] = offsetBuffer[i].map<int*>();
        getTxtContent("./dataset_" + std::to_string(PARTITION_NUM) + "pe/" + datasetName + "_row_double.txt", offsetList[i], offsetNum, false);
        // getTxtContent("./dataset_" + std::to_string(PARTITION_NUM) + "pe/" + datasetName + "_row.txt", offsetList[i], offsetNum, false);
    }

    std::cout << "synchronize input buffer data to device global memory\n";
    auto start_pcie = std::chrono::steady_clock::now();
    for (int i = 0; i < PARTITION_NUM; i++) {
        edgeBuffer[i].sync(XCL_BO_SYNC_BO_TO_DEVICE);
        columnBuffer[i].sync(XCL_BO_SYNC_BO_TO_DEVICE);
        offsetBuffer[i].sync(XCL_BO_SYNC_BO_TO_DEVICE);
    }
    auto end_pcie = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_data_transfer = end_pcie-start_pcie;
    std::cout << "PCIe time = " << elapsed_data_transfer.count() << std::endl;

    std::cout << "Execution of the kernel\n";
    int TcNum = 0;
    std::vector<xrt::bo> resultBuffer;
    std::vector<int*> result;
    resultBuffer.resize(PARTITION_NUM);
    result.resize(PARTITION_NUM);
    std::vector<xrt::run> krnl_run;
    krnl_run.resize(PARTITION_NUM);

    for (int i = 0; i < PARTITION_NUM; i++) {
        resultBuffer[i] = xrt::bo(device, sizeof(int), tc_krnl[i].group_id(0));
        result[i] = resultBuffer[i].map<int*>();
    }

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < PARTITION_NUM; i++) {
        krnl_run[i] = tc_krnl[i](edgeBuffer[i], offsetBuffer[i], offsetBuffer[i], columnBuffer[i], columnBuffer[i], edgeNum[i], COALESCE_DIST, resultBuffer[i]);
    }

    for (int i = 0; i < PARTITION_NUM; i++) {
        krnl_run[i].wait();
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_seconds = end-start;

    for (int i = 0; i < PARTITION_NUM; i++) {
        resultBuffer[i].sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        TcNum += result[i][0];
    }

    std::cout << "TC number = " << TcNum << " Time elapse " << elapsed_seconds.count() << std::endl;
    // std::cout << "TC number = " << TcNum << std::endl;
    return 0;
}
