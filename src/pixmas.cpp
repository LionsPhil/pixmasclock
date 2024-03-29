// Copyright (c) 2020 Philip Boulain; see LICENSE for terms.
#include <exception>
#include <iostream>
#include <memory>

// Force VSCode to know this is going to get the version 1 define here.
#define SDLVERSION 1
#include "hack.hpp"

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
#ifndef DESKTOP
			auto video_info = SDL_GetVideoInfo();
#endif
			framebuffer = SDL_SetVideoMode(
#ifdef DESKTOP
				// This is the resolution of the Tontec GPIO display.
				480, 320,
#else
				video_info->current_w, video_info->current_h,
#endif
				0, 0 /*| SDL_SWSURFACE | SDL_DOUBLEBUF*/);
			if(framebuffer == nullptr) { throw Error(); }
		}
		~Graphics() {
			SDL_Quit();
		}
	};
};

int main(int argc, char** argv) {
	SDL::Graphics graphics;
	SDL_WM_SetCaption("pixmas", "pixmas");
#ifndef DESKTOP
	SDL_ShowCursor(0);
#endif
	SDL_Surface* fb = graphics.framebuffer;
	SDL_FillRect(fb, NULL, SDL_MapRGB(fb->format, 0x77, 0x77, 0x77));
	SDL_Flip(fb);

	// Pick one of the factory functions from hack.hpp here.
	// TODO: Allow picking at startup or runtime.
	//std::unique_ptr<Hack::Base> hack = Hack::MakeSnowFP(fb->w, fb->h);
	//std::unique_ptr<Hack::Base> hack = Hack::MakeSnowInt(fb->w, fb->h);
	//std::unique_ptr<Hack::Base> hack = Hack::MakeSnowClock(fb->w, fb->h);
	std::unique_ptr<Hack::Base> hack = Hack::MakePopClock(fb->w, fb->h);
	//std::unique_ptr<Hack::Base> hack = Hack::MakeColorCycle(fb->w, fb->h);

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
			if (now < ticklast) {
				// Timer wraparound! You could do fancier code here to figure
				// out the interval, but eh, stutter a bit every ~49 days.
				ticklast = now;
			}
			tickerror += (now - ticklast);
			ticklast = now;
		}
		if(tickerror >= hack->tick_duration()) {
			if(tickerror > hack->tick_duration() * 10) {
				static bool once = false;
				if(!once) {
					std::cerr << "Running too slow! Skipping ticks!" << std::endl;
					once = true;
				}
				tickerror = hack->tick_duration();
			}
			do {
				tickerror -= hack->tick_duration();
				hack->simulate();
			} while(tickerror >= hack->tick_duration());

			if(hack->want_render()) {
				hack->render(fb);
				SDL_Flip(fb);
			}
		} else {
			/// Have a nap until we actually have at least one tick to run.
			SDL_Delay(hack->tick_duration());
		}
	}

	return EXIT_SUCCESS;
}
