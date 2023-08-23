all:
	gcc -Wall -o qsort qsort.c -lpthread -std=gnu99
	gcc -Wall -o hsort hsort.c
	gcc -Wall -o isort isort.c -lpthread
clean:
	rm -rf qsort heap hsort isort qsort.dump qsort.s log

check:
	./qsort -t
	./hsort
	./isort