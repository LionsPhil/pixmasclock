/* Drifting snow, floating-point version.
 * This is pretty much a port of my JS/Canvas version from Xmas 2013:
 * https://www.lionsphil.co.uk/junk/snow.html
 * It turns out the hardware FP even on the original Pi B is pretty good,
 * and it'll comfortably run this even though it's not terribly efficient.
 * It's a little smoother and easier to understand than the integer version.
 */

#include <cmath>

#include <array>
#include <random>
#include <vector>

#include "hack.hpp"

constexpr int k_snowflake_count = 1024;

namespace Hack {
struct DriftingSnow : public Hack::Base {
	int w, h;
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

		void init(DriftingSnow& h) {
			reset_common(h);
			y = h.random_y(h.generator);
			dy = h.random_frac(h.generator) - 0.5;
		}

		void reset_at_top(DriftingSnow& h) {
			reset_common(h);
			y = 0.0;
			// Snow that's drifting in from the top doesn't start aimless; it
			// starts with momentum that's bringing it on-screen. We actually
			// keep the previous momentum. (snow.js used to *only* reset y.)
			// To avoid stuff getting too lockstep, blend it with randomness.
			dy = (dy * 0.75) + ((h.random_frac(h.generator) - 0.5) * 0.25);
		}

	private:
		void reset_common(DriftingSnow& h) {
			x = h.random_x(h.generator);
			z = h.random_frac(h.generator);
			dx = h.random_frac(h.generator) - 0.5;
			// Brightness is taken from depth, rather than random like size.
			// High z is closer, thus brighter, because it's a multipler on d.
			brightness = std::ceil(255.0 * z);
			/*half_size = h.random_frac(h.generator) + 0.5;
			full_size = half_size * 2.0;*/
		}
	};
	std::array<Snowflake, k_snowflake_count> snowflakes;

	std::vector<double> breezes;

	DriftingSnow(int w, int h, SDL_PixelFormat* fmt)
		: w(w), h(h),
		random_x(0, w-1),
		random_y(0, h-1),
		random_frac(0.0, 1.0),
		breezes(h) {

		for(int i=0; i<256; ++i) {
			greyscale[i] = SDL_MapRGB(fmt, i, i, i);
		}
		for(auto&& flake : snowflakes) {
			flake.init(*this);
		}
	}

	void simulate() override {
		// Modify breezes
		// Put energy into system
		int breeze_mod_y = random_y(generator);
		breezes[breeze_mod_y] = (random_frac(generator) * 16) - 8;
		// Smooth them and lose energy
		double breeze_last = breezes[0];
		for(int y=1; y<h; ++y) {
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
			if(flake_int_y >=0 && flake_int_y < h) {
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
			if(flake.x < 0) { flake.x += w; }
			if(flake.x >= w) { flake.x -= w; }

			// Reset if out of bounds vertically
			if(flake.y > h) { flake.reset_at_top(*this); }
		}
	}
	bool render(SDL_Surface* fb) override {
		// Dirty regions only work if we can unpaint previous snowflake
		// positions, but separate simulate() makes that hard.
		SDL_FillRect(fb, 0, greyscale[0]);
		if(SDL_MUSTLOCK(fb)) { SDL_LockSurface(fb); }

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
			Uint32 color = greyscale[flake.brightness];
			SDL_FillRect(fb, &position, color);
		}

		if(SDL_MUSTLOCK(fb)) { SDL_UnlockSurface(fb); }
		return true;
	}

	Uint32 tick_duration() override { return 100; } // 10Hz
};

std::unique_ptr<Hack::Base> MakeSnowFP(int w, int h, SDL_PixelFormat* fmt) {
	return std::make_unique<DriftingSnow>(w, h, fmt);
}

}; // namespace Hack
