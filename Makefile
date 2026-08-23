CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS := -lm

.PHONY: all clean

all: can_transmitter can_dashboard

can_transmitter: can_transmitter.c
	$(CC) $(CFLAGS) $< $(LDLIBS) -o $@

can_dashboard: can_dashboard.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f can_transmitter can_dashboard
