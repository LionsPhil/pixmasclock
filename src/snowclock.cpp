/* Drifting snow, collecting upon a digital clock.
 * This is built upon (but does not inherit code in any clever way from) the
 * integer version of drifting snow.
 */

#include <cassert>
#include <cmath>
#include <ctime>

#include <array>
#include <functional>
#include <random>
#include <stdexcept>
#include <vector>

#include "hack.hpp"
#include "digitalclock.hpp"

constexpr int k_snowflake_count = 1024 * 2;
#if SDLVERSION != 1
	// Assume higher res, more powerful computer. Hacks!
	#define FAT_FLAKES
#endif

namespace Hack {
struct SnowClock : public Hack::Base {
	int w, h;
	// Build up the snow on a greyscale surface for buffering, and also we want
	// to write raw in a known pixel format rather than FillRect.
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> snowfb;
	std::default_random_engine generator;
	std::uniform_int_distribution<int> random_x;
	std::uniform_int_distribution<int> random_y;
	std::uniform_int_distribution<int> random_delay_x;
	std::uniform_int_distribution<int> random_delay_y;
	std::uniform_int_distribution<int> random_delay_b;
	std::uniform_int_distribution<int> random_delay_next_breeze;
	std::uniform_int_distribution<int> random_coinflip;
	std::uniform_int_distribution<int> random_mass;
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

	class StaticSnow {
		std::vector<Uint8> snow_;
		int w_, h_;
		/* Dummy value for silenly allowing accesses outside the bounds, instead
		 * of needing lots of perfect defensive coding when looking at adjacent
		 * pixels (that will have to invent a value on read anyway). This isn't
		 * threadsafe, but we're not threaded. */
		Uint8 out_of_bounds_;
	public:
		StaticSnow(int w, int h) : w_(w), h_(h) {
			snow_.resize(w_ * h_);
			//for(int y=50; y<h_-50; ++y) { at(50,y)=255; } // DEBUG
			//for(int x=0; x<w; ++x) { for(int y=10; y<h; ++y) { at(x,y)=255; }} // DEBUG
		}

		Uint8& at(int x, int y) {
			if(x < 0 || x >= w_ || y < 0 || y >= h_) {
				out_of_bounds_ = 0; // In case garbage was written previously.
				return out_of_bounds_;
			} else {
				// Since we've done our own bounds check, and we size this
				// precisely once in the c'tor and *shouldn't* screw that up,
				// use unsafe operator[] instead of at() to skip doing it again.
				return snow_[x + (y*w_)];
			}
		}

		// Flow as much snow as possible from 'from' to 'to' without overflow.
		static void flow(Uint8& from, Uint8& to) {
			int total = from + to;
			to = std::min(255, total);
			from = total - to;
		}

		void simulate(bool drop_bottom,
			std::function<bool(int,int)> obstacles) {
			// The bottom row of snow usually completely static once formed, but
			// when drop_bottom is true, we let it fall away.
			int start_y = h_ - (drop_bottom ? 1 : 2);
			// We continue once *something* has happened to the snow here, so it
			// only gets one change per tick.
			for(int y = start_y; y >= 0; --y) { // bottom-up makes falling natural
				for(int x = 0; x < w_; ++x) {
					Uint8& here = at(x, y);
					if(here > 0) {
						// Hit check; get crushed by obstacles
						if(obstacles(x, y)) { here = 0; }

						// Fall check
						// (An alternative would be to respawn them as flakes)
						Uint8& down = at(x, y+1);
						if((down < here) && !obstacles(x, y+1)) {
							flow(here, down);
							continue;
						}

						// Angle of repose check, must be away from walls
						// FIXME The left->right sweep means we spill left-biased anyway
						if(x > 0 && x < w_-1) {
							Uint8& down_left = at(x-1, y+1);
							bool down_left_obstacle = obstacles(x-1, y+1);
							Uint8& down_right = at(x+1, y+1);
							bool down_right_obstacle = obstacles(x+1, y+1);
							if(down_left < here && ! down_left_obstacle) {
								if(down_right < here && !down_right_obstacle) {
									// Split, 3-way flow
									int total = down_left + down_right + here;
									down_left = std::min(255, total/2);
									down_right = std::min(255, total/2);
									here = total - (down_left + down_right);
								} else {
									// Spill left
									flow(here, down_left);
								}
								continue;
							} else if (down_right < here &&
								!down_right_obstacle) {
								// Spill right
								flow(here, down_right);
								continue;
							}
						}
					}
				}
			}
		}
	};
	StaticSnow static_snow;
	DigitalClock digital_clock;

	SnowClock(int w, int h)
		: w(w), h(h),
		snowfb(nullptr, SDL_FreeSurface),
		random_x(0, w-1),
		random_y(0, h-1),
		random_delay_x(1, 20),
		random_delay_y(1, 10),
		random_delay_b(1, 3),
		random_delay_next_breeze(1, 20),
		random_coinflip(0, 1),
		random_mass(1, 255),
		breeze_delay(h),
		breeze_sign(h),
		tick(0),
		next_breeze_in(0),
		static_snow(w, h),
		digital_clock(w, h, false) {

		/* Making SDL format-convert means we don't have to at write time, and
		 * can just slap down 32-bit values. */
		snowfb.reset(SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_ASYNCBLIT,
			w, h, 8, 0, 0, 0, 0));
		if(snowfb.get() == nullptr) {
			throw std::runtime_error(SDL_GetError());
		}

		struct SDL_Color greys[256];
		for(int i=0; i<256; ++i) {
			greys[i] = {
				static_cast<Uint8>(i), static_cast<Uint8>(i), static_cast<Uint8>(i),
				0 };
		}
		if(SDL_SetColors(snowfb.get(), greys, 0, 256) != 1) {
			throw std::runtime_error("failed to set snow palette");
		}

		for(auto&& flake : snowflakes) {
			flake.init(*this);
		}
	}

	void simulate() override {
		// Get localtime
		std::time_t now_epoch = std::time(nullptr);
		std::tm* now = std::localtime(&now_epoch);
		digital_clock.set_time(now);
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
		for(int y=1; y<h; ++y) {
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
			if(flake.y >= 0 && flake.y < h &&
				breeze_sign[flake.y] != 0 &&
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
			if(flake.x < 0) { flake.x += w; }
			if(flake.x >= w) { flake.x -= w; }

			// Collide and collect with static snow/bottom of screen
			if(flake.y >= h) {
				int mass = static_snow.at(flake.x, h-1) + flake.mass;
				if(mass > 255) {
					static_snow.at(flake.x, h-2) = mass - 255;
					mass = 255;
				}
				static_snow.at(flake.x, h-1) = mass;
				// Respawn
				flake.reset_at_top(*this);
			} else if(static_snow.at(flake.x, flake.y) > 0) {
				int mass = static_snow.at(flake.x, flake.y) + flake.mass;
				if(mass > 255) {
					if(flake.y > 0) {
						static_snow.at(flake.x, flake.y-1) = mass - 255;
					}
					mass = 255;
				}
				static_snow.at(flake.x, flake.y) = mass;
				// Respawn
				flake.reset_at_top(*this);
			} else if(flake.y < 0) {
				// Hit by a breeze a the top, respawn immediately.
				flake.reset_at_top(*this);
			} else if(digital_clock.solid_at(flake.x, flake.y)) {
				// Collide with the digital clock and settle on top
				// (anything on top should collide with the gathered snow).
				Uint8& above = static_snow.at(flake.x, flake.y-1);
				above = std::min(255u, above + flake.mass);
				// Respawn
				flake.reset_at_top(*this);
			}
		}
		// Simulate the static snow
		// Drop out on the hour for 15 seconds.
		static_snow.simulate(now->tm_min == 00 && now->tm_sec < 15,
			[&](auto x, auto y){return digital_clock.solid_at(x, y);});
			// mmmmm this generates some lovely compiler errors
			/* std::bind(&DigitalClock::solid_at, digital_clock,
				std::placeholders::_1, std::placeholders::_2) */
		++tick;
	}

	void render(SDL_Surface* fb) override {
		// Dirty regions only work if we can unpaint previous snowflake
		// positions, but separate simulate() makes that hard.
		if(SDL_MUSTLOCK(snowfb.get())) { SDL_LockSurface(snowfb.get()); }
		SDL_FillRect(snowfb.get(), nullptr, 0);
		Uint8* fb2_pixels = reinterpret_cast<Uint8*>(snowfb.get()->pixels);
		auto pixel_at = [&](Sint16 x, Sint16 y){
			return (fb2_pixels + x + (y*snowfb->pitch));
		};

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
			SDL_FillRect(fb2.get(), &line, color);
		}
#endif

		for(Sint16 y=0; y<h; ++y) {
			for(Sint16 x=0; x<w; ++x) {
				if(static_snow.at(x, y)>0) {
					*pixel_at(x, y) = static_snow.at(x, y);
				}
			}
		}

		for(auto&& flake : snowflakes) {
			// Hope you're ready for some horrendous hackery.
#ifdef FAT_FLAKES
			for(Sint16 dy = -1; dy <= 1; ++dy) {
				for(Sint16 dx = -1; dx <= 1; ++dx) {
					if((dx != 0) && (dy != 0)) { continue; } // no corners
					Sint16 x = flake.x + dx;
					Sint16 y = flake.y + dy;
					unsigned int mass = flake.mass;
					//if((dx != 0) || (dy != 0)) { mass /= 2; } // "antialias"
#else
			Sint16 x = flake.x;
			Sint16 y = flake.y;
			unsigned int mass = flake.mass;
#endif
			// Skip out of bounds.
			if(x < 0 || x >= w || y < 0 || y >= h)
				{ continue; }
			unsigned int bright = std::min(255u,
				mass +  *pixel_at(x, y));
			*pixel_at(x, y) = bright;
#ifdef FAT_FLAKES
				}
			}
#endif
		}

		if(SDL_MUSTLOCK(snowfb.get())) { SDL_UnlockSurface(snowfb.get()); }
		SDL_BlitSurface(snowfb.get(), nullptr, fb, nullptr);
		// Merge in the digital clock, which is color-keyed for transparency.
		SDL_BlitSurface(digital_clock.rendered(), nullptr, fb, nullptr);
	}

	Uint32 tick_duration() override { return 100; } // 10Hz
};

std::unique_ptr<Hack::Base> MakeSnowClock(int w, int h) {
	return std::make_unique<SnowClock>(w, h);
}

}; // namespace Hack
