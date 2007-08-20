CFLAGS += $(shell pkg-config --cflags bitlbee) -g -Wall
LDFLAGS += $(shell pkg-config --libs bitlbee)

skype.so: skype.c
	gcc -o skype.so -shared skype.c $(CFLAGS)

clean:
	rm -f skype.so

doc: HEADER.html Changelog

HEADER.html: README
	ln -s README HEADER.txt
	asciidoc -a toc -a numbered HEADER.txt
	rm HEADER.txt

Changelog: .git/refs/heads/master
	git log --no-merges > Changelog
