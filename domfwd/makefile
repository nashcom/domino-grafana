CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS  = -lcurl

# Targets
TARGET  = domfwd
TARGET_TEST = wal_test

# Common sources
WAL_SRC  = simple_wal.cpp
WAL_HDR  = simple_wal.hpp

all: $(TARGET)

test: $(TARGET_TEST)

domfwd: domfwd.cpp $(WAL_SRC) $(WAL_HDR)
	$(CXX) $(CXXFLAGS) -o $@ domfwd.cpp $(WAL_SRC) $(LDFLAGS)

wal_test: wal_test.cpp $(WAL_SRC) $(WAL_HDR)
	$(CXX) $(CXXFLAGS) -o $@ wal_test.cpp $(WAL_SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
	rm -f $(TARGET_TEST)

