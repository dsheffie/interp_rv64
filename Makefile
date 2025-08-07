UNAME_S = $(shell uname -s)

OBJ = tage_base.o main.o elf.o disassemble.o helper.o interpret.o saveState.o githash.o syscall.o raw.o fdt.o temu_code.o virtio.o uart.o trace.o nway_cache.o branch_predictor.o av.o

ifeq ($(UNAME_S),Linux)
	CXX = clang++-16 -march=native
	EXTRA_LD = -ldl -lunwind -lboost_program_options -lcapstone
endif

ifeq ($(UNAME_S),FreeBSD)
	CXX = CC -march=native
	EXTRA_LD = -L/usr/local/lib -lunwind -lboost_program_options -lcapstone
endif

ifeq ($(UNAME_S),Darwin)
	CXX = clang++ -march=native -I/opt/local/include
	EXTRA_LD = -L/opt/local/lib -lboost_program_options-mt -lcapstone
endif

CXXFLAGS = -std=c++11 -g $(OPT)
LIBS =  $(EXTRA_LD) -lpthread

DEP = $(OBJ:.o=.d)
OPT = -O3 -flto -std=c++11 -g
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
