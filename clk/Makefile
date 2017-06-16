# status: implements/tested only features required by Xilinx HDMI

ccflags-y += -Iinclude
ccflags-y += -DFALSE=0 -Dllabs=abs
ccflags-y += -Wno-unused-variable

obj-m    += si5324.o

si5324-y := clk-si5324.o si5324drv.o

