CFLAGS += $(shell pkg-config --cflags bitlbee) -g

skype.so: skype.c
	gcc -o skype.so -shared skype.c $(CFLAGS)

clean:
	rm -f skype.so
