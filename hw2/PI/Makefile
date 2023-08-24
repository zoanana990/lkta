CFLAGS := -std=gnu11 -Wall -g -O2 -D_GNU_SOURCE
LDFLAGS := -lpthread

all: main.c atomic.h  cond.h  futex.h  mutex.h  spinlock.h
	$(CC) $(CFLAGS) main.c -o test $(LDFLAGS)

# Test suite
NAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    PRINTF = printf
else
    PRINTF = env printf
endif
PASS_COLOR = \e[32;01m
NO_COLOR = \e[0m
notice = $(PRINTF) "$(PASS_COLOR)$(strip $1)$(NO_COLOR)\n"

check: all
	sudo ./test

clean:
	$(RM) ./test
.PHONY: clean