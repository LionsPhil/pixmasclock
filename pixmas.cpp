// vim: ts=4
// Blah blah Philip Boulain blah copyright blah 2020 blah 3-CLAUSE BSD LICENSE
#include <cassert>
#include <cmath>

//#include <algorithm>
#include <exception>
//#include <iostream>
#include <random>
//#include <set>
//#include <sstream>
//#include <vector>

#include <SDL.h>

const Uint32 tickduration = 100; // 10Hz

// This is just used to get SDL init/deinit via RAII for nice error handling
namespace SDL {
	struct Error : public std::exception {
		virtual const char* what() const throw()
			{ return SDL_GetError(); }
	};

	struct Graphics {
		SDL_Surface* framebuffer;

		Graphics() {
			if(SDL_Init(SDL_INIT_VIDEO) != 0) { throw Error(); }
			auto video_info = SDL_GetVideoInfo();
			framebuffer = SDL_SetVideoMode(
				video_info->current_w, video_info->current_h, 0, SDL_SWSURFACE);
			if(framebuffer == nullptr) { throw Error(); }
		}
		~Graphics() {
			SDL_Quit();
		}
	};
};

struct Hack {
	std::default_random_engine generator;
	std::uniform_int_distribution<int> distribution;

	Hack() : distribution(0,100){}

	void simulate() {
		// Eh, do it all in render.
	}
	void render(SDL_Surface* fb) {
		SDL_Rect line = {
			static_cast<Sint16>(distribution(generator)),
			static_cast<Sint16>(distribution(generator)),
			1, 1 };
		SDL_FillRect(fb, &line, 0x00ffffff);
		SDL_UpdateRect(fb, line.x, line.y, 1, 1);
	}
};

int main(int argc, char** argv) {
	SDL::Graphics graphics;
	SDL_WM_SetCaption("pixmas", "pixmas");
	SDL_Surface* fb = graphics.framebuffer;
	SDL_FillRect(fb, NULL, 0x00777777);
	SDL_UpdateRect(fb, 0, 0, 0, 0);

	Hack hack;

	Uint32 tickerror = 0;
	Uint32 ticklast = SDL_GetTicks();
	SDL_Event event;
	bool run = true;
	while(run) {
		// Process events.
		while(SDL_PollEvent(&event)) { switch(event.type) {
			case SDL_QUIT:
				run = false; break;
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym) {
					case SDLK_ESCAPE:
					case SDLK_q:
						 run = false; break;
					default:; // Don't care.
				}
			default:; // Don't care.
		}}

		// Process the passage of time.
		{
			const Uint32 now = SDL_GetTicks();
			tickerror += (now - ticklast);
			ticklast = now;
		}
		if(tickerror >= tickduration) {
			do {
				tickerror -= tickduration;
				hack.simulate();
			} while(tickerror >= tickduration);
			hack.render(fb);
		} else {
			/// Have a nap until we actually have at least one tick to run.
			SDL_Delay(tickduration);
		}
	}

	return EXIT_SUCCESS;
}
