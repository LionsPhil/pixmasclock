// vim: ts=4
// Blah blah Philip Boulain blah copyright blah 2020 blah 3-CLAUSE BSD LICENSE
#include <cassert>
#include <cmath>

//#include <algorithm>
#include <array>
#include <exception>
//#include <iostream>
#include <random>
//#include <set>
//#include <sstream>
#include <vector>

#include <SDL.h>

constexpr int k_snowflake_count = 512;

// This is just used to get SDL init/deinit via RAII for nice error handling --
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
#ifdef DESKTOP
				// This is the resolution of the Tontec GPIO display.
				480, 320,
#else
				video_info->current_w, video_info->current_h,
#endif
				0, SDL_SWSURFACE);
			if(framebuffer == nullptr) { throw Error(); }
		}
		~Graphics() {
			SDL_Quit();
		}
	};
};

// Graphical hack itself ------------------------------------------------------
// (This could be an abstract base class and all, but bleh, quick and dirty)

struct Hack {
	const Uint32 tickduration = 100; // 10Hz

	SDL_Surface* fb;
	std::default_random_engine generator;
	std::uniform_int_distribution<int> random_x;
	std::uniform_int_distribution<int> random_y;
	std::uniform_real_distribution<double> random_frac;
	std::array<Uint32, 256> greyscale;

	struct Snowflake {
		double x, y, z, dx, dy;
		int brightness;
		// These aren't very meaningful without antialiasing, hence
		// brightness instead.
		//double half_size, full_size;

		void init(Hack& h) {
			reset_at_top(h);
			y = h.random_y(h.generator);
		}

		void reset_at_top(Hack& h) {
			x = h.random_x(h.generator);
			y = 0.0;
			z = h.random_frac(h.generator);
			dx = h.random_frac(h.generator) - 0.5;
			dy = h.random_frac(h.generator) - 0.5;
			// Brightness is taken from depth, rather than random like size.
			brightness = std::ceil(255.0 * (1.0-z));
			/*half_size = h.random_frac(h.generator) + 0.5;
			full_size = half_size * 2.0;*/
		}
	};
	std::array<Snowflake, k_snowflake_count> snowflakes;

	std::vector<double> breezes;

	Hack(SDL_Surface* framebuffer)
		: fb(framebuffer),
		random_x(0, framebuffer->w-1),
		random_y(0, framebuffer->h-1),
		random_frac(0.0, 1.0),
		breezes(framebuffer->h) {

		for(int i=0; i<256; ++i) {
			greyscale[i] = SDL_MapRGB(fb->format, i, i, i);
		}
		for(auto&& flake : snowflakes) {
			flake.init(*this);
		}
	}

	void simulate() {
		// Modify breezes
		// Put energy into system
		int breeze_mod_y = random_y(generator);
		breezes[breeze_mod_y] = (random_frac(generator) * 16) - 8;
		// Smooth them and lose energy
		double breeze_last = breezes[0];
		for(int y=1; y<fb->h; ++y) {
			double breeze = breezes[y];
			// Avoid whipping up into a frenzied storm by capping
			if(breeze < -8) { breeze = -8; }
			if(breeze > 8)  { breeze =  8; }
			// Smooth
			breezes[y - 1] = (breeze_last * 0.899) + (breeze * 0.100);
			breezes[y]     = (breeze_last * 0.100) + (breeze * 0.899);
			// Depower peaks a little more
			if(breeze > 1) { breezes[y] *= 0.99; }
			// Next
			breeze_last = breezes[y];
		}

		// Move flakes
		for(auto&& flake : snowflakes) {
			// Accellerate due to gravity up to terminal velocity
			if(flake.dy < 2) { flake.dy += 0.1; }

			// Accellerate to match breeze, gain lift from it
			int flake_int_y = std::round(flake.y);
			if(flake_int_y >=0 && flake_int_y < breezes.size()) {
				double breeze = breezes[flake_int_y];
				double breeze_abs = std::abs(breeze);
				if(breeze < flake.dx) {
					flake.dx -= breeze_abs * 0.2;
					flake.dy -= breeze_abs * random_frac(generator) * 0.1;
				}
				if(breeze > flake.dx) {
					flake.dx += breeze_abs * 0.2;
					flake.dy -= breeze_abs * random_frac(generator) * 0.1;
				}
			}

			// Decellerate and twist in imaginary vorticies if above terminal
			// velocity (maybe the lift is good enough for this)

			// Move
			// TODO: Consider precomputing these multiplies instead.
			flake.x += flake.dx * flake.z;
			flake.y += flake.dy * flake.z;

			// Wrap horizontally
			if(flake.x < 0) { flake.x += fb->w; }
			if(flake.x >= fb->w) { flake.x -= fb->w; }

			// Reset if out of bounds vertically
			if(flake.y > fb->h) { flake.reset_at_top(*this); }
		}
	}
	void render() {
		// Dirty regions only work if we can unpaint previous snowflake
		// positions, but separate simulate() makes that hard.
		SDL_FillRect(fb, 0, greyscale[0]);

		for(auto&& flake : snowflakes) {
			// We don't anti-alias.
			SDL_Rect position {
				static_cast<Sint16>(std::round(flake.x)),
				static_cast<Sint16>(std::round(flake.y)),
				1, 1};
			// Skip out of bounds.
			if(position.x < 0 || position.x >= fb->w
				|| position.y < 0 || position.y >= fb->h)
				{ continue; }
			// TODO: This could be faster if hitting the pixel data raw.
			SDL_FillRect(fb, &position, greyscale[flake.brightness]);
		}

		SDL_UpdateRect(fb, 0, 0, 0, 0);
		/*SDL_Rect line = {
			static_cast<Sint16>(random_x(generator)),
			static_cast<Sint16>(random_y(generator)),
			1, 1 };
		SDL_FillRect(fb, &line, SDL_MapRGB(fb->format, 0xff, 0xff, 0xff));
		SDL_UpdateRect(fb, line.x, line.y, 1, 1);*/
	}
};

// Drive the hack -------------------------------------------------------------
// ("Hack" here being used in the same sense as xscreensaver: some neat code to
// do a pretty thing.)

int main(int argc, char** argv) {
	SDL::Graphics graphics;
	SDL_WM_SetCaption("pixmas", "pixmas");
#ifndef DESKTOP
	SDL_ShowCursor(0);
#endif
	SDL_Surface* fb = graphics.framebuffer;
	SDL_FillRect(fb, NULL, SDL_MapRGB(fb->format, 0x77, 0x77, 0x77));
	SDL_UpdateRect(fb, 0, 0, 0, 0);

	Hack hack(fb);

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
		if(tickerror >= hack.tickduration) {
			do {
				tickerror -= hack.tickduration;
				hack.simulate();
			} while(tickerror >= hack.tickduration);
			hack.render();
		} else {
			/// Have a nap until we actually have at least one tick to run.
			SDL_Delay(hack.tickduration);
		}
	}

	return EXIT_SUCCESS;
}
