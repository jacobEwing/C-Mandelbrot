CC=gcc
CFLAGS=-Wall -O3 -march=native -fopenmp
LIBS=-lSDL2 -lgmp -lm

OBJS=main.o mandelbrot.o renderer.o

all: mandelbrot

mandelbrot: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o mandelbrot $(LIBS)

main.o: main.c renderer.h mandelbrot.h df64.h
	$(CC) $(CFLAGS) -c main.c

mandelbrot.o: mandelbrot.c mandelbrot.h df64.h
	$(CC) $(CFLAGS) -c mandelbrot.c

renderer.o: renderer.c renderer.h mandelbrot.h df64.h
	$(CC) $(CFLAGS) -c renderer.c

clean:
	rm -f *.o mandelbrot
