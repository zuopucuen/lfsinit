CC = gcc
CFLAGS = -O2
MAKE = $(CC) $(CFLAGS)
all:
	$(MAKE) -o init init.c
	$(MAKE) -o reboot reboot.c
	$(MAKE) -o shutdown shutdown.c

clean:
	rm -f init reboot shutdown
