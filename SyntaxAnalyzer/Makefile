all:
	bison -d parser.y
	flex scanner.l
	gcc -o parser symtable.c parser.tab.c lex.yy.c

clean:
	rm -f parser parser.tab.c parser.tab.h lex.yy.c
