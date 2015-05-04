CC = gcc
FLAGS = -Wall -g
DEPS = lyph.h srv.h jsonfmt.h jsonfmt_internal.h mallocf.h

all: lyph

lyph: labels.o srv.o util.o trie.o ucl.o nt_parse.o jsonfmt.o lyph.o mallocf.o tables.o cmds.o meta.o hier.o lyph.h
	$(CC) $(FLAGS) labels.o srv.o util.o trie.o ucl.o nt_parse.o jsonfmt.o lyph.o mallocf.o tables.o cmds.o meta.o hier.o -g -o lyph

%.o: %.c $(DEPS)
	$(CC) $(FLAGS) -o $@ -c $<

clean:
	rm -f *.o
