# Femc Driver
# Copyright (C) 2015-2018 Pauli Saksa
#
# Licensed under The MIT License, see file LICENSE.txt in this source tree.

.PHONY : all
all :

FEMC_DRIVER_ROOT=.
include $(FEMC_DRIVER_ROOT)/Makefile.flags

# driver targets

DRIVER_LIB_OBJECTS =dispatcher.o error_stack.o s11n.o utils.o http.o can.o
DRIVER_OBJECTS =$(DRIVER_LIB_OBJECTS) dns-service.o
DRIVER_BINARIES=dns-service
DRIVER_TARGETS=libfemc-driver.a

#

OBJECTS =$(DRIVER_OBJECTS)
BINARIES=$(DRIVER_BINARIES)
TARGETS =$(DRIVER_TARGETS)

# subdirectories

include app/Makefile.in
include demo/Makefile.in

#

ALL_TARGETS=$(OBJECTS) $(BINARIES) $(TARGETS)

CPPFLAGS=$(CONFIGURATION_FLAGS) -I.
CFLAGS  =$(EXTRA_COMPILE_FLAGS) -Wall -Wextra -Werror
LDFLAGS =$(EXTRA_LINK_FLAGS)
ARFLAGS =rsuU

all : print-dir $(ALL_TARGETS)

# driver binaries

dns-service : dns-service.o
libfemc-driver.a : libfemc-driver.a( $(DRIVER_LIB_OBJECTS) )

# rules

.PHONY : print-dir
print-dir :
	@echo / $(notdir $(shell pwd))

$(BINARIES) :
	@echo -e 'LINK:\t' $@
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY : clean distclean
clean distclean : print-dir
	$(RM) $(ALL_TARGETS) $(OBJECTS:.o=.o.d)

include $(FEMC_DRIVER_ROOT)/Makefile.rules
