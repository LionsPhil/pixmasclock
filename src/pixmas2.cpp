// Copyright (c) 2020 Philip Boulain; see LICENSE for terms.
#include <exception>
#include <iostream>
#include <memory>

// Force VSCode to know this is going to get the version 2 define here.
#define SDLVERSION 2
#include "hack.hpp"

// This is just used to get SDL init/deinit via RAII for nice error handling.
namespace SDL {
	struct Error : public std::exception {
		virtual const char* what() const throw()
			{ return SDL_GetError(); }
	};

	struct Graphics {
		SDL_Window* window;
		SDL_Renderer *renderer;
		SDL_Texture* texture;
		int w, h;

		Graphics() {
			if(SDL_Init(SDL_INIT_VIDEO) != 0) { throw Error(); }
			if(SDL_CreateWindowAndRenderer(
#ifdef DESKTOP
				// Resolution of the Pimoroni HyperPixel.
				800, 480,
				0,
#else
				// Counterintuitively, the non-"desktop" behaviour is fullscreen.
				0, 0,
				SDL_WINDOW_FULLSCREEN_DESKTOP,
#endif
				&window, &renderer) != 0 ) { throw Error(); }
			SDL_SetWindowTitle(window, "pixmas");
#ifndef DESKTOP
			SDL_SetWindowAlwaysOnTop(window, SDL_TRUE);
#endif
			if(SDL_GetRendererOutputSize(renderer, &w, &h) != 0)
				{ throw Error(); }
			texture = SDL_CreateTexture(renderer,
							SDL_PIXELFORMAT_ARGB8888,
							SDL_TEXTUREACCESS_STREAMING,
							w, h);
		}
		~Graphics() {
			// Let SDL free all its own things.
			SDL_Quit();
		}
	};
};

int main(int argc, char** argv) {
	SDL::Graphics graphics;
#ifndef DESKTOP
	SDL_ShowCursor(0);
#endif
	SDL_SetRenderDrawColor(graphics.renderer, 0x77, 0x77, 0x77, 0xff);
	SDL_RenderClear(graphics.renderer);
	SDL_RenderPresent(graphics.renderer);

	SDL_Surface* tmp_fb = nullptr;
	if(SDL_LockTextureToSurface(graphics.texture, NULL, &tmp_fb) != 0)
		{ throw std::runtime_error(SDL_GetError()); }

	// Pick one of the factory functions from hack.hpp here.
	// TODO: Allow picking at startup or runtime.
	//std::unique_ptr<Hack::Base> hack = Hack::MakeSnowFP(graphics.w, graphics.h, tmp_fb->format);
	//std::unique_ptr<Hack::Base> hack = Hack::MakeSnowInt(graphics.w, graphics.h, tmp_fb->format);
	//std::unique_ptr<Hack::Base> hack = Hack::MakeSnowClock(graphics.w, graphics.h);
	// WORKS
	std::unique_ptr<Hack::Base> hack = Hack::MakePopClock(graphics.w, graphics.h);
	//std::unique_ptr<Hack::Base> hack = Hack::MakeColorCycle();

	SDL_UnlockTexture(graphics.texture);

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
			SDL_Surface* fb = nullptr;
			if(SDL_LockTextureToSurface(graphics.texture, NULL, &fb) != 0)
				{ throw std::runtime_error(SDL_GetError()); }
			bool rendered = hack->render(fb);
			SDL_UnlockTexture(graphics.texture); // Also frees fb
			fb = nullptr;
			if(rendered) {
				SDL_RenderClear(graphics.renderer);
				SDL_RenderCopy(graphics.renderer, graphics.texture,
					NULL, NULL);
				SDL_RenderPresent(graphics.renderer);
			}
		} else {
			/// Have a nap until we actually have at least one tick to run.
			SDL_Delay(hack->tick_duration());
		}
	}

	return EXIT_SUCCESS;
}
