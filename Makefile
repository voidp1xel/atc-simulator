# ══════════════════════════════════════════════════════════════════════
#  ATC Sector Simulator – Makefile
#
#  Compiles the C++17 backend into a single executable.
#  The frontend is served as static files by the built-in HTTP server.
#
#  Targets:
#    make          – build the simulator
#    make clean    – remove object files and binary
#    make run      – build and run on default port 8080
# ══════════════════════════════════════════════════════════════════════

CXX       = g++
CXXFLAGS  = -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS   = -pthread

SRC = backend/main.cpp          \
      backend/Server.cpp        \
      backend/SimulationEngine.cpp \
      backend/Aircraft.cpp      \
      backend/PhysicsMath.cpp   \
      backend/Collision.cpp

OBJ = $(SRC:.cpp=.o)
TARGET = atc-simulator

# ── Default target ───────────────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^
	@echo "\n✅  Build complete: ./$(TARGET)"
	@echo "    Run with:  ./$(TARGET) [port]"

# ── Pattern rule for .cpp → .o ───────────────────────────────────────
backend/%.o: backend/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ── Convenience: build and run ───────────────────────────────────────
run: $(TARGET)
	./$(TARGET) 8080

# ── Clean ────────────────────────────────────────────────────────────
clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean run
