all: httpd client
LIBS = -lpthread #-lsocket
httpd: httpd.c base_type.h
	gcc -g -W -Wall $(LIBS) -o $@ $<

client: simpleclient.c base_type.h
	gcc -W -Wall -o $@ $<
clean:
	rm httpd
