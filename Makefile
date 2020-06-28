CXX = g++

PROGRAMS = dealer example_player my_player_A my_player_B my_player_2.5

all: $(PROGRAMS)

clean:
	rm -f $(PROGRAMS)

clean_log:
	rm -f ./logs/*

dealer: game.cpp game.h evalHandTables rng.cpp rng.h dealer.cpp net.cpp net.h
	$(CXX) -o $@ game.cpp rng.cpp dealer.cpp net.cpp

example_player: game.cpp game.h evalHandTables rng.cpp rng.h example_player.cpp net.cpp net.h
	$(CXX) -o $@ game.cpp rng.cpp example_player.cpp net.cpp

my_player_A: game.cpp game.h evalHandTables rng.cpp rng.h my_player_A.cpp net.cpp net.h
	$(CXX) -o $@ game.cpp rng.cpp my_player_A.cpp net.cpp

my_player_B: game.cpp game.h evalHandTables rng.cpp rng.h my_player_B.cpp net.cpp net.h
	$(CXX) -o $@ game.cpp rng.cpp my_player_B.cpp net.cpp

my_player_2.5: game.cpp game.h evalHandTables rng.cpp rng.h my_player_2.5.cpp net.cpp net.h
	$(CXX) -o $@ game.cpp rng.cpp my_player_2.5.cpp net.cpp