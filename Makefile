CXX      := g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -pthread
LDFLAGS  := -pthread

OBJS := main.o data_parallel.o task_parallel.o
BIN  := allocator

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp flight.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all run clean
