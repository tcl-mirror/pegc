#!/usr/bin/make -f
# Requires GNU Make 3.80+!
default: all
CPPFLAGS ?= -g
# -Wall -Werror

PACKAGE.NAME = pegc
PACKAGE.VERSION := $(shell date +%Y%m%d)
-include shake-n-make.make

#CFLAGS += -std=c99
#CFLAGS += -xc99=all # SUN CC

INCLUDES += -I.
HASH_OBJ := $(patsubst %,%.o,hashtable hashtable_itr hashtable_utility)
PEGC_OBJ := clob.o pegc.o vappendf.o $(HASH_OBJ)
test.BIN.OBJECTS = $(PEGC_OBJ) test.o

$(call ShakeNMake.CALL.RULES.BINS,test)
bins: test.BIN

CLEAN_FILES += *~

all: bins
