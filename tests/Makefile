CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter
LDLIBS  ?= -libverbs

BINS := implicit_odp_reg_test implicit_odp_write_test \
        implicit_odp_multi_test implicit_odp_cross_test

all: $(BINS)

implicit_odp_reg_test: implicit_odp_reg_test.c helpers.h
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

implicit_odp_write_test: implicit_odp_write_test.c helpers.h
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

implicit_odp_multi_test: implicit_odp_multi_test.c helpers.h
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

implicit_odp_cross_test: implicit_odp_cross_test.c helpers.h
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f $(BINS)

.PHONY: all clean
