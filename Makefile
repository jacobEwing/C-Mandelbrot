CC=gcc
CXX=g++
CFLAGS=-Wall -O3 -march=native -fopenmp
CXXFLAGS=-Wall -O3 -march=native -fopenmp -Iimgui -Iimgui/backends $(shell sdl2-config --cflags)
LIBS=-lSDL2 -lgmp -lm

IMGUI_SRCS = imgui/imgui.cpp \
             imgui/imgui_draw.cpp \
             imgui/imgui_tables.cpp \
             imgui/imgui_widgets.cpp \
             imgui/backends/imgui_impl_sdl2.cpp \
             imgui/backends/imgui_impl_sdlrenderer2.cpp

IMGUI_OBJS = $(IMGUI_SRCS:.cpp=.o)

all: mandelbrot

mandelbrot: main.o mandelbrot.o renderer.o $(IMGUI_OBJS)
	$(CXX) $(CXXFLAGS) main.o mandelbrot.o renderer.o $(IMGUI_OBJS) -o mandelbrot $(LIBS)

main.o: main.cpp renderer.h mandelbrot.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

mandelbrot.o: mandelbrot.c mandelbrot.h df64.h
	$(CC) $(CFLAGS) -c mandelbrot.c -o mandelbrot.o

renderer.o: renderer.c renderer.h mandelbrot.h df64.h
	$(CC) $(CFLAGS) -c renderer.c -o renderer.o

imgui/%.o: imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o mandelbrot
	rm -f imgui/*.o imgui/backends/*.o
