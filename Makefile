# It's been long enough I CBA to write proper production rules
pixmas:
	clang++ `pkg-config sdl --cflags --libs` -O -Wall -Werror pixmas.cpp -o pixmas
