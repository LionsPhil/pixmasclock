/* Drifting snow, integer version.
 * This is a port of the floating point version to use tick delays instead of
 * fractional velocities, which means it can be pure integer arithmetic and
 * run a little faster, which is used to generate four times as many snowflakes
 * for about the same performance.
 */

#include <cmath>

#include <array>
#include <random>
#include <vector>

#include <SDL.h>

#include "hack.hpp"

constexpr int k_snowflake_count = 4096;

namespace Hack {
struct SnowInt : public Hack::Base {
	SDL_Surface* fb;
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

	struct Snowflake {
		Sint16 x, y, dx; // dx is sign only
		// Inverse of dx/dy, i.e. how many ticks between each step.
		// delay_t is the terminal velocity, the smallest delays can get.
		unsigned int delay_x, delay_y, delay_t;
		unsigned int mass;

		void init(SnowInt& h) {
			reset_common(h);
			y = h.random_y(h.generator);
			delay_y = h.random_delay_y(h.generator);
		}

		void reset_at_top(SnowInt& h) {
			reset_common(h);
			y = 0;
			// Stop things getting too lockstep.
			delay_y /= 2;
			delay_y += 1 + (h.random_delay_y(h.generator) / 2);
		}

	private:
		void reset_common(SnowInt& h) {
			x = h.random_x(h.generator);
			dx = h.random_coinflip(h.generator) == 1 ? 1 : -1;
			delay_x = h.random_delay_x(h.generator);
			mass = h.random_mass(h.generator);
			delay_t = ((255-mass) / 25) + 1;
		}
	};
	std::array<Snowflake, k_snowflake_count> snowflakes;

	SnowInt(SDL_Surface* framebuffer)
		: fb(framebuffer),
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
		next_breeze_in(0) {

		for(int i=0; i<256; ++i) {
			greyscale[i] = SDL_MapRGB(fb->format, i, i, i);
		}
		for(auto&& flake : snowflakes) {
			flake.init(*this);
		}
	}

	int ss_at(int x, int y) { return x + fb->w * y; }

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
				if(breeze_delay[y] > 100) {
					breeze_sign[y] = 0;
					breeze_delay[y] = 100;
				} else {
					// Decay
					++breeze_delay[y];
				}
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

			// Wrap
			if(flake.x < 0) { flake.x += fb->w; }
			if(flake.x >= fb->w) { flake.x -= fb->w; }
			if(flake.y > fb->h) { flake.reset_at_top(*this); }
		}
		++tick;
	}
	void render() override {
		// Dirty regions only work if we can unpaint previous snowflake
		// positions, but separate simulate() makes that hard.
		SDL_FillRect(fb, 0, greyscale[0]);
		if(SDL_MUSTLOCK(fb)) { SDL_LockSurface(fb); }

		// Debug breezes
#ifdef DEBUG_BREEZES
		for(int y=0; y<fb->h; ++y) {
			if(breeze_sign[y] == 0) { continue; }
			Uint8 d = std::max(static_cast<int>(255 - 2*breeze_delay[y]), 0);
			SDL_Rect line { 0, static_cast<Sint16>(y),
				static_cast<Uint16>(fb->w), 1};
			Uint32 color;
			if(breeze_sign[y] < 0) {
				color = SDL_MapRGB(fb->format, d, 0, 255);
			} else {
				color = SDL_MapRGB(fb->format, 0, d, 255);
			}
			SDL_FillRect(fb, &line, color);
		}
#endif

		for(auto&& flake : snowflakes) {
			SDL_Rect position { flake.x, flake.y, 1, 1};
			// Skip out of bounds.
			if(position.x < 0 || position.x >= fb->w
				|| position.y < 0 || position.y >= fb->h)
				{ continue; }
			unsigned int bright = flake.mass;
			SDL_FillRect(fb, &position, greyscale[bright]);
		}

		if(SDL_MUSTLOCK(fb)) { SDL_UnlockSurface(fb); }
		SDL_Flip(fb);
	}

	Uint32 tick_duration() override { return 100; } // 10Hz
};

std::unique_ptr<Hack::Base> MakeSnowInt(SDL_Surface* framebuffer) {
	return std::make_unique<SnowInt>(framebuffer);
}

}; // namespace Hack
