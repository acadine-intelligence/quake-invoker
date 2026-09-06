# Host-side tests for engine-independent game rules.
#   make -C tests        build and run
#   make -C tests clean
CC ?= cc
CFLAGS ?= -Wall -Wextra -O1
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BIN := $(ROOT)/build/tests

all: run

$(BIN)/test_bg_invoke: test_bg_invoke.c qi_host_shim.h $(ROOT)/code/game/bg_invoke.c $(ROOT)/code/game/bg_invoke.h
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) -DQI_HOST_TEST -I$(ROOT)/tests -I$(ROOT)/code/game test_bg_invoke.c $(ROOT)/code/game/bg_invoke.c -o $@

run: $(BIN)/test_bg_invoke
	$(BIN)/test_bg_invoke

clean:
	rm -rf $(BIN)

.PHONY: all run clean
