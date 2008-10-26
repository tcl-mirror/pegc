#!/usr/bin/make -f
# Requires GNU Make 3.80+!
default: all

ifeq (1,$(TCC))
  # If you have (want to use) tcc, try this:
  CC := tcc
  CXX := tcc
  CPPFLAGS += -gb -bt 10 \
	-Wwrite-strings \
	-Wunsupported \
	-Wall
else
  # Assume gcc-compatible flags:
  CXXFLAGS += -g
  CPPFLAGS += \
	-Wimplicit-function-declaration \
	-Wall \
	-g
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
HASH_OBJ := $(patsubst %,%.o,whhash)
libwhhash.LIB.OBJECTS = $(HASH_OBJ)
$(call ShakeNMake.CALL.RULES.LIBS,libwhhash)

########################################################################
# GC lib:
WHGC_OBJ := whgc.o
libwhgc.LIB.OBJECTS = $(WHGC_OBJ) $(HASH_OBJ)
$(call ShakeNMake.CALL.RULES.LIBS,libwhgc)
$(libwhgc.LIB): $(libwhhash.LIB)

########################################################################
# pegc lib:
libpegc.LIB.OBJECTS := clob.o vappendf.o pegc.o
ifeq (1,1)
  libpegc.LIB.OBJECTS += $(libwhgc.LIB.OBJECTS)
else
  libpegc.LIB.OBJECTS += $(libwhgc.LIB) $(libwhhash.LIB)
endif
$(call ShakeNMake.CALL.RULES.LIBS,libpegc)
$(libpegc.LIB): $(libwhgc.LIB)
test.BIN.LDFLAGS := $(libpegc.LIB)
test.BIN.OBJECTS := test.o
$(call ShakeNMake.CALL.RULES.BINS,test)
$(test.BIN): $(libpegc.LIB)

pegcgen.BIN.LDFLAGS := $(libpegc.LIB)
pegcgen.BIN.OBJECTS := pegcgen.o # $(libpegc.LIB.OBJECTS)
$(call ShakeNMake.CALL.RULES.BINS,pegcgen)
$(pegcgen.BIN): $(libpegc.LIB)

bins: $(test.BIN)
bins: $(pegcgen.BIN)
libs: $(libpegc.LIB)
CLEAN_FILES += *~

PACKAGE.DIST_FILES += $(wildcard *.c *.h)

all: libs bins
