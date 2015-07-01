CC = gcc
CPPC = g++
FLAGS = -Wall -Werror -g
DEPS = lyph.h srv.h jsonfmt.h jsonfmt_internal.h mallocf.h macro.h

all: lyph

lyph: labels.o srv.o util.o trie.o ucl.o nt_parse.o jsonfmt.o lyph.o mallocf.o tables.o cmds.o meta.o hier.o csv.o fromjs.opp fma.o
	$(CPPC) $(FLAGS) labels.o srv.o util.o trie.o ucl.o nt_parse.o jsonfmt.o lyph.o mallocf.o tables.o cmds.o meta.o hier.o csv.o fma.o fromjs.opp -o lyph

%.o: %.c $(DEPS)
	$(CC) $(FLAGS) -o $@ -c $<

%.opp: %.cpp $(DEPS)
	$(CPPC) $(FLAGS) -o $@ -c $<

clean:
	rm -f *.o
	rm -f *.opp
