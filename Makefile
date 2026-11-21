CC=gcc
CXX=g++
CFLAGS=-Wall -O3 -march=native -fopenmp -Icjson
CXXFLAGS=-Wall -O3 -march=native -fopenmp -Iimgui -Iimgui/backends -Icjson $(shell sdl2-config --cflags)
LIBS=-lSDL2 -lgmp -lmpfr -lpng -lz -lm

IMGUI_SRCS = imgui/imgui.cpp \
             imgui/imgui_draw.cpp \
             imgui/imgui_tables.cpp \
             imgui/imgui_widgets.cpp \
             imgui/backends/imgui_impl_sdl2.cpp \
             imgui/backends/imgui_impl_sdlrenderer2.cpp

IMGUI_OBJS = $(IMGUI_SRCS:.cpp=.o)

all: mandelbrot animandel

mandelbrot: main.o mandelbrot.o renderer.o state.o cjson/cJSON.o $(IMGUI_OBJS)
	$(CXX) $(CXXFLAGS) main.o mandelbrot.o renderer.o state.o cjson/cJSON.o $(IMGUI_OBJS) -o mandelbrot $(LIBS)

animandel: animandel.o anim.o mandelbrot.o renderer.o state.o cjson/cJSON.o
	$(CXX) $(CXXFLAGS) animandel.o anim.o mandelbrot.o renderer.o state.o cjson/cJSON.o -o animandel $(LIBS)

main.o: main.cpp renderer.h mandelbrot.h state.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

anim.o: anim.c anim.h state.h mandelbrot.h
	$(CC) $(CFLAGS) -c anim.c -o anim.o

animandel.o: animandel.c anim.h
	$(CC) $(CFLAGS) -c animandel.c -o animandel.o

mandelbrot.o: mandelbrot.c mandelbrot.h df64.h
	$(CC) $(CFLAGS) -c mandelbrot.c -o mandelbrot.o

renderer.o: renderer.c renderer.h mandelbrot.h df64.h
	$(CC) $(CFLAGS) -c renderer.c -o renderer.o

state.o: state.c state.h mandelbrot.h cjson/cJSON.h
	$(CC) $(CFLAGS) -c state.c -o state.o

cjson/cJSON.o: cjson/cJSON.c cjson/cJSON.h
	$(CC) $(CFLAGS) -c cjson/cJSON.c -o cjson/cJSON.o

imgui/%.o: imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o mandelbrot animandel
	rm -f imgui/*.o imgui/backends/*.o cjson/*.o
