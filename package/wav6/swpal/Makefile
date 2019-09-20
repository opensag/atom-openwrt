# libwlan source Makefile
PKG_NAME := libspal
 
bins := libspal.so

libspal.so_sources := $(wildcard src/*.c)
	
libspal.so_cflags := -I./src/include
libspal.so_ldflags := -L./src/ -luci -lsafec-1.0

include make.inc
