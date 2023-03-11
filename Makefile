INCLUDE = -I$(ROOT_BASE)/include
INCLUDE += -Iinclude

LIBS = -L$(ROOT_BASE)/lib


LDLIBS = `root-config --cflags --glibs` -pthread -fopenmp -lcapnp -lkj
 

all:
	g++ dispacher.cpp -o build src/*.cpp -std=c++17 $(INCLUDE) $(LIBS) $(LDLIBS)	
	g++ builder.cpp -o builder src/*.cpp -std=c++17 $(INCLUDE) $(LIBS) $(LDLIBS)	
