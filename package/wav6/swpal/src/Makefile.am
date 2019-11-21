ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libswpal.la

libswpal_la_SOURCES = uci_wrapper.c

libswpal_la_LDFLAGS = -luci

AM_CPPFLAGS = -Wall -Werror -I${top_srcdir}/include $(CPPFLAGS) -DPACKAGE_ID=\"WAVE_HAL\" -DLOGGING_ID -DLOG_LEVEL=6 -DLOG_TYPE=1

pkgconfigdir = $(libdir)/pkgconfig
include_HEADERS = $(srcdir)/include/uci_wrapper.h \
                  $(srcdir)/include/help_logging.h
