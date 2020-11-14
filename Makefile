.PHONY: clean runonpi

# It's been long enough I CBA to write proper production rules
pixmas:
	clang++ `pkg-config sdl --cflags --libs` -O -Wall -Werror pixmas.cpp -o pixmas

clean:
	rm -f pixmas

runonpi: pixmas
	sudo SDL_VIDEODRIVER="fbcon" SDL_FBDEV="/dev/fb1" ./pixmas
