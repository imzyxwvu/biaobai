LIBS=-lsqlite3 -lgd -lm -fPIC

all: register compose

register: register.c
	gcc -o $@ -O2 $< $(LIBS)

compose: compose.c
	gcc -o $@ -O2 $< $(LIBS)

