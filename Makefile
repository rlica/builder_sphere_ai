CPP = g++

INCLUDE = -I$(ROOT_BASE)/include
INCLUDE += -Iinclude

LIBS = -L$(ROOT_BASE)/lib


LDLIBS = -lTree -lRIO -lCore -lHist -pthread -fopenmp -ltbb -lcapnp-rpc -lcapnp -lkj-async -lkj -g
#LDLIBS = `root-config --cflags --glibs` -fopenmp -lcapnp -lkj

SOURCE_DIR := src

SOURCE_CPP := $(wildcard $(SOURCE_DIR)/*.cpp)
SOURCE_C++ := $(wildcard $(SOURCE_DIR)/*.c++)

OBJECTS_CPP := $(SOURCE_CPP:$(SOURCE_DIR)/%.cpp=$(SOURCE_DIR)/%.o)
OBJECTS_C++ := $(SOURCE_C++:$(SOURCE_DIR)/%.c++=$(SOURCE_DIR)/%.o)

OBJECTS := $(patsubst $(SOURCE_DIR)/%.cpp, $(SOURCE_DIR)/%.o, $(SOURCE_CPP))
OBJECTS += $(patsubst $(SOURCE_DIR)/%.cpp, $(SOURCE_DIR)/%.o, $(OBJECTS_C++))

FLAGS = -std=c++17 -O3 -march=native


all: $(OBJECTS)
	$(CPP)  dispacher.cpp $^ $(INCLUDE) $(LIBS) $(FLAGS) $(LDLIBS) -o build
	$(CPP)  builder.cpp $^ $(INCLUDE) $(LIBS) $(FLAGS) $(LDLIBS) -o builder 

$(SOURCE_DIR)/%.o: $(SOURCE_DIR)/%.cpp 
	$(CPP) $(INCLUDE) -c $< -o $@ $(FLAGS)

$(SOURCE_DIR)/%.o: $(SOURCE_DIR)/%.c++ 
	$(CPP) $(INCLUDE) -c $< -o $@ $(FLAGS)



cap: 
	capnp compile -oc++ eventProto.capnp		
	mv eventProto.capnp.h include/eventProto.capnp.h
	mv eventProto.capnp.c++ src/eventProto.capnp.c++

clean:
	@rm -rf $(SOURCE_DIR)/*.o builder build