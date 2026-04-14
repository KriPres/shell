CC = gcc
CFLAGS = -std=c99 -g -Wvla -Wall -fsanitize=address,undefined

all: mysh

mysh: mysh.o arraylist.o linestream.o fileutils.o 
	$(CC) $(CFLAGS) -o $@ $^

mysh.o: mysh.c arraylist.h
	$(CC) $(CFLAGS) -c $<

fileutils.o: fileutils.c fileutils.h arraylist.h
	$(CC) $(CFLAGS) -c $<

arraylist.o: arraylist.c arraylist.h
	$(CC) $(CFLAGS) -c $<

linestream.o: linestream.c linestream.h
	$(CC) $(CFLAGS) -c -DBUFLEN=8  $< -o $@

clean:
	rm -f *.o mysh
