CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
SRC = src/server.cpp src/net.cpp src/game.cpp src/poker.cpp
HDR = src/util.h src/json.h src/poker.h src/net.h src/game.h

pokerserver: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o pokerserver $(SRC)

clean:
	rm -f pokerserver pokerserver.cgi

run: pokerserver
	./pokerserver 8080

.PHONY: clean run
