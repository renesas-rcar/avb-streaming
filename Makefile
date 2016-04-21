#
# Makefile for the Renesas device drivers.
#

ifndef CONFIG_AVB_STREAMING
CONFIG_RAVB_STREAMING ?= m
CONFIG_RAVB_STREAMING_FTRACE_DESC ?= n
CONFIG_RAVB_STREAMING_FTRACE_LOCK ?= n
CONFIG_RAVB_EXT_STATS ?= m
endif

CFLAGS_ravb_streaming_main.o := -I$(src)

ravb_streaming-objs := ravb_streaming_main.o
ravb_streaming-objs += ravb_streaming_avbtool.o
ravb_streaming-objs += ravb_streaming_sysfs.o

obj-$(CONFIG_RAVB_STREAMING) += ravb_streaming.o
obj-$(CONFIG_RAVB_EXT_STATS) += ravb_proc.o

ifndef CONFIG_AVB_STREAMING
SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) $@

endif
