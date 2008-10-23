#!/usr/bin/make -f
# Requires GNU Make 3.80+!
default: all
CPPFLAGS ?= -g
# -Wall -Werror

PACKAGE.NAME = pegc
PACKAGE.VERSION := $(shell date +%Y%m%d)
# ShakeNMake.DOXYGEN.USE_DOT := 1
-include shake-n-make.make

#CFLAGS += -std=c99
#CFLAGS += -xc99=all # SUN CC

#LLIST_GENERATOR := ./makeLinkList.sh
#LLIST_TEMPLATES := pegc_llist.h pegc_llist.c
#pegc_llist_long.h: Makefile $(LLIST_GENERATOR) $(LLIST_TEMPLATES)
#	bash $(LLIST_GENERATOR) pegc_llist_long long
#pegc_llist_long.c: pegc_llist_long.h
#pegc_llist_long.o: pegc_llist_long.c pegc_llist_long.h

INCLUDES += -I.
HASH_OBJ := $(patsubst %,%.o,hashtable hashtable_itr hashtable_utility)
PEGC_OBJ := clob.o pegc.o vappendf.o $(HASH_OBJ)
test.BIN.OBJECTS = $(PEGC_OBJ) test.o

$(call ShakeNMake.CALL.RULES.BINS,test)
bins: test.BIN

CLEAN_FILES += *~

all: bins
