# Makefile - ECE 470 Project 2 (DineSync)
# Yousef Alhindi

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I src -pthread
SRC      = src
TEST     = test

.PHONY: all test clean

all: hub_server test_p2

# Hub Server
hub_server: $(SRC)/hub_server.cpp $(SRC)/hub_protocol.h $(SRC)/tcp_transport.h $(SRC)/data_model.h
	$(CXX) $(CXXFLAGS) -o hub_server $(SRC)/hub_server.cpp

# P2 Integration Tests
test_p2: $(TEST)/test_p2.cpp $(SRC)/hub_protocol.h $(SRC)/tcp_transport.h $(SRC)/data_model.h $(SRC)/protocol_messages.h $(SRC)/protocol_handler.h
	$(CXX) $(CXXFLAGS) -o test_p2 $(TEST)/test_p2.cpp

# Run tests
test: test_p2
	./test_p2

clean:
	rm -f hub_server test_p2
