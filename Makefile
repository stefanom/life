CXX = g++
CXXBASE = -std=c++17 -Wall -Wextra -I include -isystem third_party
CXXFLAGS = $(CXXBASE) -O3 -march=native -flto
LDFLAGS = -flto

ENGINE_SRCS = src/engine.cpp src/engine_hashtable.cpp src/engine_sorted_vector.cpp src/engine_hashlife.cpp
ENGINE_HDRS = include/engine.h

.PHONY: all clean test debug san benchmark benchmark-engines

all: game_of_life test

game_of_life: src/main.cpp src/game_of_life.cpp src/renderer.cpp $(ENGINE_SRCS) include/game_of_life.h include/renderer.h $(ENGINE_HDRS)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp src/game_of_life.cpp src/renderer.cpp $(ENGINE_SRCS) $(LDFLAGS)

test_game_of_life: test/test_game_of_life.cpp src/game_of_life.cpp src/renderer.cpp $(ENGINE_SRCS) include/game_of_life.h include/renderer.h $(ENGINE_HDRS)
	$(CXX) $(CXXFLAGS) -o $@ test/test_game_of_life.cpp src/game_of_life.cpp src/renderer.cpp $(ENGINE_SRCS) $(LDFLAGS)

benchmark_bin: test/benchmark.cpp src/game_of_life.cpp $(ENGINE_SRCS) include/game_of_life.h $(ENGINE_HDRS)
	$(CXX) $(CXXFLAGS) -o $@ test/benchmark.cpp src/game_of_life.cpp $(ENGINE_SRCS) $(LDFLAGS)

benchmark_engines_bin: test/benchmark_engines.cpp src/game_of_life.cpp $(ENGINE_SRCS) include/game_of_life.h $(ENGINE_HDRS)
	$(CXX) $(CXXFLAGS) -o $@ test/benchmark_engines.cpp src/game_of_life.cpp $(ENGINE_SRCS) $(LDFLAGS)

test: test_game_of_life
	./test_game_of_life

benchmark: benchmark_bin
	./benchmark_bin

benchmark-engines: benchmark_engines_bin
	./benchmark_engines_bin

debug: CXXFLAGS = $(CXXBASE) -g -O0
debug: clean game_of_life test_game_of_life

san: CXXFLAGS = $(CXXBASE) -g -O1 -fsanitize=address,undefined
san: LDFLAGS += -fsanitize=address,undefined
san: clean game_of_life test_game_of_life

clean:
	rm -f game_of_life test_game_of_life benchmark_bin benchmark_engines_bin
