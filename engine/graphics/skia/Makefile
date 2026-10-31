# skia probe build (FR-003 / AC-GRAPHICS-039, selection leg).
# Targets: all | host | select_present | clean
# - `host` compiles the hermetic T1 selection TU (`probe_skia.cpp`) with the
#   host g++ (no Skia SDK headers/link needed; selection only, no GPU
#   context). Engine-side only, no package GPU symbols (FR-PKG-007).
# - `select_present` compiles the batch's exact contract test
#   `tests/select_present.test.cpp` (shared T1/T2 file, owned by
#   code-unit-tester, never edited here). It needs T2's `present_skia.h`
#   alongside T1's `probe_skia.h`, so this target goes green once T2 lands.
# - `all` (default) builds host + select_present.
# All builds run in the worktree, never the workspace.

SRC = probe_skia.cpp
TEST_SRC = tests/select_present.test.cpp
HOST_OUT = probe_skia.o
TEST_OUT = build/select_present

CXX_STD = -std=c++17
WARN = -Wall -Wextra -Werror
INCLUDES = -I .

.PHONY: all host select_present clean

all: host select_present

host:
	g++ $(CXX_STD) $(WARN) -O2 $(INCLUDES) -c $(SRC) -o $(HOST_OUT)

select_present:
	mkdir -p build
	g++ $(CXX_STD) $(WARN) -O2 $(INCLUDES) $(SRC) present_skia.cpp $(TEST_SRC) -o $(TEST_OUT)
	./$(TEST_OUT)

clean:
	rm -f $(HOST_OUT) $(TEST_OUT)
