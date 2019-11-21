builddir = .

override CC := $(abs_top)/support/mtlkfront.sh $(CC)
override AM_CFLAGS = -Wall -Wno-multichar -fno-strict-aliasing -Werror -Wno-error=undef
override CFLAGS += -O2 -Wno-multichar 
override CFLAGS += -I$(abs_top)
override CFLAGS += -I$(builddir)/./
override CFLAGS += -I$(abs_top)/tools/mttools/shared
override CFLAGS += -I$(abs_top)/tools/mttools/shared/linux/
override CFLAGS += -I$(abs_top)/tools/shared/
override CFLAGS += -I$(abs_top)/tools/shared/linux/
override CFLAGS += -I$(abs_top)/tools/mtidl/
override CFLAGS += -I$(abs_top)/wireless/shared/
override CFLAGS += -I$(abs_top)/include/config/
override CFLAGS += -I$(abs_top)/tools/rtlogger/shared/
override CFLAGS += -I$(abs_top)/wireless/shared_mbss_mac/
override CFLAGS += -DHAVE_CONFIG_H -include $(abs_top)/include/config/config.h
override CFLAGS += -DMTLK_SOURCE_VERSION="\"1.0\""
override CFLAGS += -DLOG_LOCAL_OID=MTLKFRONT_WILL_FILL_IT
LINK = $(LDFLAGS) $(AM_CFLAGS) $(CFLAGS) -o $@

objs = mtdump.o mtidl_reader.o \
		logmacro_database.o

deps =  $(abs_top)/tools/mttools/shared/libmttools.a \
	$(abs_top)/tools/shared/linux/libmtlkc.a \
	$(abs_top)/wireless/shared/libmtlkwls.a \
	$(abs_top)/tools/shared/3rd_party/iniparser/libiniparser.a

mtdump_LDADD =  $(abs_top)/tools/mttools/shared/libmttools.a \
		$(abs_top)/tools/shared/linux/libmtlkc.a \
		$(abs_top)/wireless/shared/libmtlkwls.a \
		$(abs_top)/tools/shared/3rd_party/iniparser/libiniparser.a

all: mtdump mtdump.mtidlc

mtdump: $(objs) $(deps)
	$(CC) $(LINK) $(objs) $(mtdump_LDADD)

mtdump.mtidlc: $(abs_top)/tools/mtidl/mtidl_ini.pl $(abs_top)/wireless/shared_mbss_mac/mhi_statistics.h $(abs_top)/wireless/shared/mtlkwssa_drvinfo.h
	perl $(abs_top)/tools/mtidl/mtidl_ini.pl mtdump $(abs_top)/wireless/shared_mbss_mac/mhi_statistics.h $(abs_top)/wireless/shared/mtlkwssa_drvinfo.h

clean:
	rm -f $(objs) mtdump mtdump.mtidlc

export logger_origin_name = mtdump
export logger_binary_name = mtdump.o
export logger_origin_id = 6
