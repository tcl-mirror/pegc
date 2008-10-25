#!/usr/bin/make -f
# Requires GNU Make 3.80+!
default: all

ifeq (1,$(TCC))
  # If you have (want to use) tcc, try this:
  CC := tcc
  CXX := tcc
  CPPFLAGS += -g \
	-Wunsupported \
	-Wall
else
  # Assume gcc-compatible flags:
  CPPFLAGS += \
	-Wimplicit-function-declaration \
	-Wall \
	-g -O0
endif

# CPPFLAGS ?= -g
# -Wall -Werror

PACKAGE.NAME = pegc
PACKAGE.VERSION := $(shell date +%Y%m%d)
# ShakeNMake.DOXYGEN.USE_DOT := 1
-include shake-n-make.make

#CFLAGS += -std=c99
#CFLAGS += -xc99=all # SUN CC

INCLUDES += -I.

########################################################################
# Hashtable lib:
HASH_OBJ := $(patsubst %,%.o,hashtable hashtable_itr hashtable_utility)
libhashtable.LIB.OBJECTS = $(HASH_OBJ)
$(call ShakeNMake.CALL.RULES.LIBS,libhashtable)

########################################################################
# GC lib:
WHGC_OBJ := whgc.o
libwhgc.LIB.OBJECTS = $(WHGC_OBJ) $(HASH_OBJ)
$(call ShakeNMake.CALL.RULES.LIBS,libwhgc)
$(libwhgc.LIB): $(libhashtable.LIB)

########################################################################
# pegc lib:
libpegc.LIB.OBJECTS := clob.o vappendf.o pegc.o
ifeq (1,1)
  libpegc.LIB.OBJECTS += $(libwhgc.LIB.OBJECTS) $(libhashtable.LIB.OBJECTS)
else
  libpegc.LIB.OBJECTS += $(libwhgc.LIB) $(libhashtable.LIB)
endif
$(call ShakeNMake.CALL.RULES.LIBS,libpegc)
$(libpegc.LIB): $(libwhgc.LIB)
test.BIN.LDFLAGS := $(libpegc.LIB)
test.BIN.OBJECTS := test.o
$(call ShakeNMake.CALL.RULES.BINS,test)
$(test.BIN): $(libpegc.LIB)
bins: test.BIN

pegcgen.BIN.LDFLAGS := $(libpegc.LIB)
pegcgen.BIN.OBJECTS := pegcgen.o
$(call ShakeNMake.CALL.RULES.BINS,pegcgen)
$(pegcgen.BIN): $(libpegc.LIB)
bins: pegcgen.BIN

CLEAN_FILES += *~

PACKAGE.DIST_FILES += $(wildcard *.c *.h)

all: bins
