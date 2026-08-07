# SPDX-License-Identifier: Apache-2.0
#
# The build of one wapp. A wapp directory sets NAME and includes this file.

WASM  := $(NAME).wasm
CLANG ?= /opt/wasi-sdk/bin/clang

IPC := ../../ipc

# --compress-relocations: without it the linker pads the table index of
# call_indirect to five bytes, and the runtime of the engine rejects that.
LDFLAGS = -Wl,-export-dynamic -Wl,--initial-memory=65536 \
          -Wl,--max-memory=65536 -z stack-size=8192 \
          -Wl,--strip-all -Wl,--compress-relocations

# _POSIX_C_SOURCE: the strict C99 mode of the libc hides clock_gettime and
# nanosleep without it.
CFLAGS = --target=wasm32-wasip1 -Os -std=c99 -D_POSIX_C_SOURCE=200809L \
         -Wall -Wextra -Werror -I../include -I$(IPC)/include

SRCS := $(wildcard *.c) $(wildcard $(IPC)/src/*.c)

.PHONY: all clean

all: $(WASM)

# The flags come from this file, thus a change of it rebuilds the wapp.
$(WASM): $(SRCS) $(MAKEFILE_LIST)
	$(CLANG) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@

clean:
	rm -f *.wasm
