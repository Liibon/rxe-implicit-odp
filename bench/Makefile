CFLAGS ?= -O2 -Wall -Wextra
LDLIBS ?= -libverbs

all: bench_reg_latency

bench_reg_latency: bench_reg_latency.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f bench_reg_latency

.PHONY: all clean
