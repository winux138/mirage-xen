ifneq (, $(shell command -v opam))
	PKG_CONFIG_PATH ?= $(shell opam var prefix)/lib/pkgconfig
endif

CC ?= cc
FREESTANDING_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags ocaml-freestanding)
CFLAGS := $(FREESTANDING_CFLAGS) \
    -O2 -std=c99 -Wall -Werror \
    -Iinclude -D__XEN_INTERFACE_VERSION__=__XEN_LATEST_INTERFACE_VERSION__

.PHONY: all clean
all: libmirage-xen_bindings.a

OBJS=atomic_stubs.o barrier_stubs.o bmap.o clock_stubs.o cstruct_stubs.o evtchn.o gnttab.o main.o

libmirage-xen_bindings.a: $(OBJS)
	$(AR) r $@ $^

clean:
	$(RM) $(OBJS) libmirage-xen_bindings.a
