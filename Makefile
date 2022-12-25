INCLUDE = -I$(ROOT_BASE)/include
INCLUDE += -Iinclude

LIBS = -L$(ROOT_BASE)/lib


LDLIBS = -lTree -lRIO -lCore -lHist -pthread -fopenmp


all:
	g++ dispacher.cpp -o build src/*.cpp -std=c++17 $(INCLUDE) $(LIBS) $(LDLIBS)	
	g++ builder.cpp -o builder src/*.cpp -std=c++17 $(INCLUDE) $(LIBS) $(LDLIBS)	
