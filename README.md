Requirements
------------

(1) Pandas package


### Compile FPGA code:
```sh
cd acc_fpga/ && make all TARGET=hw_emu PLATFORM=/opt/xilinx/platforms/xilinx_u250_gen3x16_xdma_3_1_202020_1/xilinx_u250_gen3x16_xdma_3_1_202020_1.xpfm
```

### Run FPGA code:
```sh
cd acc_fpga/ && ./host.exe -xclbin "xclbin file" -o ./data/offset.txt -i ./data/colomn.txt
```

### For example:
```sh
./host.exe -xclbin TC_kernel.xclbin -o amazon0312/amazon0312.mtx_offset.txt -i amazon0312/amazon0312.mtx_column.txt 
```


