/* Drifting snow, collecting upon a digital clock.
 * This is built upon (but does not inherit code in any clever way from) the
 * integer version of drifting snow.
 */

#include <cmath>

#include <array>
#include <random>
#include <vector>

#include <SDL.h>

#include "hack.hpp"

/* Unfortunately, even with the extra buffering, 4K is flickery on real
 * hardware. I'm not really sure why. */
constexpr int k_snowflake_count = 2048;

namespace Hack {
struct SnowClock : public Hack::Base {
	SDL_Surface* fb;
	// Manual double-buffering, because on fbdev it doesn't seem to work, and
	// also we want to write raw in a known pixel format rather than FillRect.
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> fb2;
	std::default_random_engine generator;
	std::uniform_int_distribution<int> random_x;
	std::uniform_int_distribution<int> random_y;
	std::uniform_int_distribution<int> random_delay_x;
	std::uniform_int_distribution<int> random_delay_y;
	std::uniform_int_distribution<int> random_delay_b;
	std::uniform_int_distribution<int> random_delay_next_breeze;
	std::uniform_int_distribution<int> random_coinflip;
	std::uniform_int_distribution<int> random_mass;
	std::array<Uint32, 256> greyscale;
	std::vector<unsigned int> breeze_delay;
	std::vector<int> breeze_sign;
	unsigned int tick;
	unsigned int next_breeze_in;
	std::vector<Uint8> static_snow;

	struct Snowflake {
		Sint16 x, y, dx; // dx is sign only
		// Inverse of dx/dy, i.e. how many ticks between each step.
		// delay_t is the terminal velocity, the smallest delays can get.
		unsigned int delay_x, delay_y, delay_t;
		unsigned int mass;

		void init(SnowClock& h) {
			reset_common(h);
			y = h.random_y(h.generator);
			delay_y = h.random_delay_y(h.generator);
		}

		void reset_at_top(SnowClock& h) {
			reset_common(h);
			y = 0;
			// Stop things getting too lockstep.
			delay_y /= 2;
			delay_y += 1 + (h.random_delay_y(h.generator) / 2);
		}

	private:
		void reset_common(SnowClock& h) {
			x = h.random_x(h.generator);
			dx = h.random_coinflip(h.generator) == 1 ? 1 : -1;
			delay_x = h.random_delay_x(h.generator);
			mass = h.random_mass(h.generator);
			delay_t = ((255-mass) / 25) + 1;
		}
	};
	std::array<Snowflake, k_snowflake_count> snowflakes;

	SnowClock(SDL_Surface* framebuffer)
		: fb(framebuffer),
		fb2(nullptr, SDL_FreeSurface),
		random_x(0, framebuffer->w-1),
		random_y(0, framebuffer->h-1),
		random_delay_x(1, 20),
		random_delay_y(1, 10),
		random_delay_b(1, 3),
		random_delay_next_breeze(1, 20),
		random_coinflip(0, 1),
		random_mass(1, 255),
		breeze_delay(framebuffer->h),
		breeze_sign(framebuffer->h),
		tick(0),
		next_breeze_in(0),
		static_snow(framebuffer->w * framebuffer->h) {

		/* Making SDL format-convert means we don't have to at write time, and
		 * can just slap down 32-bit values. */
		fb2.reset(SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_ASYNCBLIT,
			fb->w, fb->h, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000));
		if(fb2.get() == nullptr) {
			throw std::bad_alloc();
		}

		for(int i=0; i<256; ++i) {
			greyscale[i] = SDL_MapRGB(fb2->format, i, i, i);
		}
		for(auto&& flake : snowflakes) {
			flake.init(*this);
		}
	}

//	int ss_at(int x, int y) { return x + fb->w * y; }

	void simulate() override {
		// Modify breezes
		if(next_breeze_in == 0) {
			// Put energy into system
			int breeze_mod_y = random_y(generator);
			breeze_delay[breeze_mod_y] = random_delay_b(generator);
			breeze_sign[breeze_mod_y] = random_coinflip(generator) == 1 ? 1 : -1;
			next_breeze_in = random_delay_next_breeze(generator);
		} else {
			--next_breeze_in;
		}
		// Smooth them and lose energy
		// TODO: not spreading downward very well, probably due to decay pass
		for(int y=1; y<fb->h; ++y) {
			//if(breeze_delay[y] % tick == 0) {
				// Share influence with predecessor
				if(breeze_sign[y-1] == breeze_sign[y]) {
					if(breeze_sign[y] == 0) {
						// Both lines are inactive.
					} else if(breeze_delay[y-1] < breeze_delay[y]) {
						++breeze_delay[y-1];
						--breeze_delay[y];
					} else if(breeze_delay[y-1] > breeze_delay[y]) {
						--breeze_delay[y-1];
						++breeze_delay[y];
					}
				} else {
					// First, the cases where one line is stationary and picks
					// up a very slow movement.
					if(breeze_sign[y-1] == 0){
						breeze_sign[y-1] = breeze_sign[y];
						breeze_delay[y-1] = breeze_delay[y]
							+ random_delay_b(generator) + 10;
					} else if(breeze_sign[y] == 0){
						breeze_sign[y] = breeze_sign[y-1];
						breeze_delay[y] = breeze_delay[y-1]
							+ random_delay_b(generator) + 10;
					} else {
						// Blowing in opposite directions on adjacent lines;
						// damp both more heavily.
						breeze_delay[y-1] += 2;
						breeze_delay[y] += 2;
					}
				}
				// Expire
				if(breeze_sign[y] == 0) { continue; }
				if(breeze_delay[y] > 100) { breeze_sign[y] = 0; }
				// Decay
				++breeze_delay[y];
			//}
		}

		// Move flakes
		for(auto&& flake : snowflakes) {
			// Breezes
			if(breeze_sign[flake.y] != 0 &&
				tick % breeze_delay[flake.y] == 0) {
				flake.x += breeze_sign[flake.y];
				--flake.y;
			}

			// Momentum
			if(tick % flake.delay_x == 0) {
				flake.x += flake.dx;
			}
			if(tick % flake.delay_y == 0) {
				++flake.y;
				// Accellerate due to gravity up to terminal velocity
				if(flake.delay_y > flake.delay_t) { --flake.delay_y; }
			}

			// Wrap horizontally
			if(flake.x < 0) { flake.x += fb->w; }
			if(flake.x >= fb->w) { flake.x -= fb->w; }

#ifdef STATIC_SNOW
			// Collide and collect with static snow/bottom of screen
			if(flake.y > fb->h) {
				int mass = static_snow[ss_at(flake.x, fb->h-1)] + flake.mass;
				if(mass > 255) {
					static_snow[ss_at(flake.x, fb->h-2)] = mass - 255;
					mass = 255;
				}
				static_snow[ss_at(flake.x, fb->h-1)] = mass;
				// Respawn
				flake.reset_at_top(*this);
			} else if(static_snow[ss_at(flake.x, flake.y)] > 0) {
				int mass = static_snow[ss_at(flake.x, flake.y)] + flake.mass;
				if(mass > 255) {
					if(flake.y > 0) {
						static_snow[ss_at(flake.x, flake.y-1)] = mass - 255;
					}
					mass = 255;
				}
				static_snow[ss_at(flake.x, flake.y)] = mass;
				// Respawn
				flake.reset_at_top(*this);
			}
#else
			if(flake.y > fb->h) { flake.reset_at_top(*this); }
#endif
		}
		++tick;
	}
	void render() override {
		// Dirty regions only work if we can unpaint previous snowflake
		// positions, but separate simulate() makes that hard.
		SDL_FillRect(fb2.get(), nullptr, greyscale[0]);
		if(SDL_MUSTLOCK(fb2.get())) { SDL_LockSurface(fb2.get()); }

		// Debug breezes
#ifdef DEBUG_BREEZES
		for(int y=0; y<fb2->h; ++y) {
			if(breeze_sign[y] == 0) { continue; }
			Uint8 d = std::max(static_cast<int>(255 - 2*breeze_delay[y]), 0);
			SDL_Rect line { 0, static_cast<Sint16>(y),
				static_cast<Uint16>(fb2->w), 1};
			Uint32 color;
			if(breeze_sign[y] < 0) {
				color = SDL_MapRGB(fb2->format, d, 0, 255);
			} else {
				color = SDL_MapRGB(fb2->format, 0, d, 255);
			}
			SDL_FillRect(fb2, &line, color);
		}
#endif

#ifdef STATIC_SNOW
		int i = 0;
		for(Sint16 y=0; y<fb2->h; ++y) {
			for(Sint16 x=0; x<fb2->w; ++x) {
				++i;
				if(static_snow[i]>0) {
					SDL_Rect position { x, y, 1, 1};
					SDL_FillRect(fb2.get(), &position, greyscale[static_snow[i]]);
				}
			}
		}
#endif

		for(auto&& flake : snowflakes) {
			SDL_Rect position { flake.x, flake.y, 1, 1};
			// Skip out of bounds.
			if(position.x < 0 || position.x >= fb2->w
				|| position.y < 0 || position.y >= fb2->h)
				{ continue; }
#ifdef STATIC_SNOW
			unsigned int bright = std::min(255u,
				flake.mass + static_snow[ss_at(flake.x, flake.y)]);
#else
			unsigned int bright = flake.mass;
#endif
			SDL_FillRect(fb2.get(), &position, greyscale[bright]);
		}

		if(SDL_MUSTLOCK(fb2.get())) { SDL_UnlockSurface(fb2.get()); }
		SDL_BlitSurface(fb2.get(), nullptr, fb, nullptr);
		SDL_Flip(fb);
	}

	Uint32 tick_duration() override { return 100; } // 10Hz
};

std::unique_ptr<Hack::Base> MakeSnowClock(SDL_Surface* framebuffer) {
	return std::make_unique<SnowClock>(framebuffer);
}

}; // namespace Hack
