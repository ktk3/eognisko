CC = gcc
CFLAGS = -Wall 
TARGETS = klient serwer 

all: $(TARGETS) 

klient: klient.o err.o err.h
	$(CC) $(CFLAGS) $^ -o $@ -levent

serwer: serwer.o err.o err.h
	$(CC) $(CFLAGS) $^ -o $@ -levent

clean:
	rm -f *.o $(TARGETS) 
