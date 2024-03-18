UNAME_S = $(shell uname -s)

OBJ = main.o disassemble.o helper.o interpret.o githash.o raw.o fdt.o temu_code.o
CXX = riscv64-unknown-elf-g++

CXXFLAGS = -std=c++11 -g $(OPT)
LIBS = -specs=htif.specs

DEP = $(OBJ:.o=.d)
OPT = -O3 -std=c++11 -mcmodel=medany 
EXE = interp_rv64

.PHONY : all clean

all: $(EXE)

$(EXE) : $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) $(LIBS) -o $(EXE)

githash.cc : .git/HEAD .git/index
	echo "const char *githash = \"$(shell git rev-parse HEAD)\";" > $@

%.o: %.cc
	$(CXX) -MMD $(CXXFLAGS) -c $< 


-include $(DEP)

clean:
	rm -rf $(EXE) $(OBJ) $(DEP)
