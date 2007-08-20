CFLAGS += $(shell pkg-config --cflags bitlbee) -g -Wall
LDFLAGS += $(shell pkg-config --libs bitlbee)

skype.so: skype.c
	gcc -o skype.so -shared skype.c $(CFLAGS)

clean:
	rm -f skype.so

doc:
	ln -s README HEADER.txt
	asciidoc -a toc -a numbered HEADER.txt
	rm HEADER.txt
