lib = $(?D)/lib$(?F:.o=.a)
def = $(?D)/$(?F:.o=.def)
imp = $(?D)/$(?F:.o=.lib)

CC = g++
AR = ar
LD = g++
CF = -c -I.
AF = -r
LF = -L.

LF_SO = -shared


prefix = /usr/include
CV_LIB = -lhighgui -lml -lcv -lcxcore -lcvaux
CV_H = -I$(prefix)/opencv
GL_LIB = -lglut -lGLU



all: libopencvar.a opencvar.so

build-test: example

test: example
	./example
	
mkpat: mkpat.o
	g++ $(LF) -o $@ $? $(CV_LIB) $(GL_LIB) -lopencvar
	
mkpat.o: mkpat.cpp
	g++ $(CF) -o $@ $? $(CV_H)

libopencvar.a : opencvar.o acmath.o acgl.o
	ar $(AF) $@ $?
	
opencvar.o : opencvar.cpp
	g++ $(CF) -o $@ $? $(CV_H)
	
	
acmath.o : acmath.cpp
	g++ $(CF) -o $@ $?
	
acgl.o : acgl.cpp
	g++ $(CF) -o $@ $?

opencvar.so : opencvar.o
	g++ $(LF_SO) -o $@ $?
	
example : example.o
	g++ $(LF) -o $@ $? $(CV_LIB) $(GL_LIB) -lopencvar
	
example.o : example.cpp
	g++ $(CF) -o $@ $? $(CV_H)
	
example-so : example.o
	g++ $(LF) -o $@ $? -l:opencvar.so