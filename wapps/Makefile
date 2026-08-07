# SPDX-License-Identifier: Apache-2.0

# Every directory here holds one wapp of the edge MCU.
WAPPS := $(patsubst %/,%,$(dir $(wildcard */Makefile)))

.PHONY: all clean $(WAPPS)

all: $(WAPPS)

$(WAPPS):
	$(MAKE) -C $@

clean:
	@for d in $(WAPPS); do $(MAKE) -C $$d clean; done
