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

#define PARTITION_NUM 8

void getTxtSize (std::string file_name, int& lineNum) {
    std::cout << "Getting file size ... " << file_name << std::endl;
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
    parser.addSwitch("--device_id", "-d", "device index", "0");
    parser.parse(argc, argv);

    // Read settings
    std::string binaryFile = parser.value("xclbin_file");
    int device_index = stoi(parser.value("device_id"));

    if (argc < 3) {
        parser.printHelp();
        return EXIT_FAILURE;
    }

    std::cout << "Open the device" << device_index << std::endl;
    auto device = xrt::device(device_index);
    std::cout << "Load the xclbin " << binaryFile << std::endl;
    auto uuid = device.load_xclbin(binaryFile);
    auto krnl = xrt::kernel(device, uuid, "TriangleCount");

    std::cout << "load graph dataset" << std::endl;

    std::string datasetName = "facebook_combined";
    std::cout << " Read edge file ... " << std::endl;

    std::fstream edge_file;
    std::string edgeName = "./dataset/" + datasetName + "_edge.txt";
    int edgeNum = 0;
    getTxtSize(edgeName, edgeNum);

    std::vector<int> columnNum;
    std::vector<int> offsetNum;
    std::vector<std::string> columnName;
    std::vector<std::string> offsetName;
    columnNum.resize(PARTITION_NUM);
    offsetNum.resize(PARTITION_NUM);
    columnName.resize(PARTITION_NUM);
    offsetName.resize(PARTITION_NUM);

    for (int i = 0; i < PARTITION_NUM; i++) {
        std::string offset_name_t = "./dataset/" + datasetName + "_row_" + std::to_string(i) + ".txt";
        std::string column_name_t = "./dataset/" + datasetName + "_col_" + std::to_string(i) + ".txt";
        int offset_num_t = 0, column_num_t = 0;
        getTxtSize(offset_name_t, offset_num_t);
        getTxtSize(column_name_t, column_num_t);
        columnNum[i] = column_num_t;
        offsetNum[i] = offset_num_t;
        columnName[i] = column_name_t;
        offsetName[i] = offset_name_t;
    }

    std::cout << " allocate memory in device" << std::endl;
    int edge_size_bytes = edgeNum * 2 * sizeof(int);
    auto edgeBuffer = xrt::bo(device, edge_size_bytes, krnl.group_id(0));
    int* edgeList = edgeBuffer.map<int*>();
    getTxtContent(edgeName, edgeList, edgeNum, true);

    std::vector<xrt::bo> columnBuffer;
    std::vector<xrt::bo> offsetBuffer;
    std::vector<int*> columnList;
    std::vector<int*> offsetList;
    columnBuffer.resize(PARTITION_NUM);
    offsetBuffer.resize(PARTITION_NUM);
    columnList.resize(PARTITION_NUM);
    offsetList.resize(PARTITION_NUM);
    for (int i = 0; i < PARTITION_NUM; i++) {
        int size_bytes = columnNum[i] * sizeof(int);
        columnBuffer[i] = xrt::bo(device, size_bytes, krnl.group_id(1));
        columnList[i] = columnBuffer[i].map<int*>();
        getTxtContent(columnName[i], columnList[i], columnNum[i], false);

        size_bytes = offsetNum[i] * sizeof(int);
        offsetBuffer[i] = xrt::bo(device, size_bytes, krnl.group_id(3));
        offsetList[i] = offsetBuffer[i].map<int*>();
        getTxtContent(offsetName[i], offsetList[i], offsetNum[i], false);
    }

    std::cout << "synchronize input buffer data to device global memory\n";
    edgeBuffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    for (int i = 0; i < PARTITION_NUM; i++) {
        columnBuffer[i].sync(XCL_BO_SYNC_BO_TO_DEVICE);
        offsetBuffer[i].sync(XCL_BO_SYNC_BO_TO_DEVICE);
    }

    std::cout << "Execution of the kernel\n";
    int TcNum = 0;
    auto resultBuffer = xrt::bo(device, sizeof(int), krnl.group_id(0));
    auto result = resultBuffer.map<int*>();

    // auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < PARTITION_NUM; i++) {
        resultBuffer.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        auto start = std::chrono::steady_clock::now();
        auto run = krnl(edgeBuffer, offsetBuffer[i], offsetBuffer[i], columnBuffer[i], columnBuffer[i], edgeNum, resultBuffer);
        run.wait();
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end-start;
        std::cout << elapsed_seconds.count() << std::endl;
        resultBuffer.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        TcNum += result[0];
    }
    // auto end = std::chrono::steady_clock::now();
    // std::chrono::duration<double> elapsed_seconds = end-start;

    // std::cout << "TC number = " << TcNum << " Time elapse " << elapsed_seconds.count() << std::endl;
    std::cout << "TC number = " << TcNum << std::endl;
    return 0;
}
