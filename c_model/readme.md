### Compile FPGA code in U250 node:
```sh
make all TARGET=hw_emu PLATFORM=/opt/xilinx/platforms/xilinx_u250_gen3x16_xdma_3_1_202020_1/xilinx_u250_gen3x16_xdma_3_1_202020_1.xpfm
```

### Run FPGA code in hw_emu:
```sh
export XCL_EMULATION_MODE=hw_emu
./tc_host -x build_dir.hw_emu.xilinx_u250_gen3x16_xdma_3_1_202020_1/tc.xclbin
```

