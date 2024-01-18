set top_name "TriangleCount"
set design_files {"tc_dach.cpp"}
set tb_files {"tc_dach.cpp"}
set parts {xczu3eg-sbva484-1-e}
set inc_dir "../src/dach"
set sol_name "solution1"
set t_clk 4
set m_axi_latency 0
set m_axi_bitwidth 128

open_project -reset "proj_${top_name}"
add_files -cflags "-I ${inc_dir}" ${design_files}
add_files -tb -cflags "-I ${inc_dir}" ${tb_files}
set_top "${top_name}"
open_solution -flow_target vitis -reset ${sol_name}
set_part ${parts}
create_clock -period ${t_clk}
config_interface -m_axi_latency ${m_axi_latency}
config_interface -m_axi_max_bitwidth ${m_axi_bitwidth}
config_interface -m_axi_max_widen_bitwidth ${m_axi_bitwidth}

csim_design
exit
