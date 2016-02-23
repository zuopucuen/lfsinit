CC=gcc -O2 
edit:
	$(CC) -o init init.c
	$(CC) -o reboot reboot.c
	$(CC) -o shutdown shutdown.c

clean:
	rm -f init reboot shutdown
