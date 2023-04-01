INCLUDE = -I$(ROOT_BASE)/include
INCLUDE += -Iinclude

LIBS = -L$(ROOT_BASE)/lib


#LDLIBS = -lTree -lRIO -lCore -lHist -pthread -fopenmp -lcapnp -lkj
LDLIBS = `root-config --cflags --glibs` -fopenmp -lcapnp -lkj


all:

	capnp compile -oc++ eventProto.capnp		
	mv eventProto.capnp.h include/eventProto.capnp.h
	mv eventProto.capnp.c++ src/eventProto.capnp.c++
	g++ dispacher.cpp -o build src/*.cpp -std=c++17 $(INCLUDE) $(LIBS) $(LDLIBS)	
	g++ builder.cpp -o builder src/*.cpp -std=c++17 $(INCLUDE) $(LIBS) $(LDLIBS)	
