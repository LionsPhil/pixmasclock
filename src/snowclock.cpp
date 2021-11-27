/* Drifting snow, collecting upon a digital clock.
 * This is built upon (but does not inherit code in any clever way from) the
 * integer version of drifting snow.
 */

#include <cmath>
#include <ctime>

#include <array>
#include <random>
#include <stdexcept>
#include <vector>

#include <SDL.h>

#include "hack.hpp"

constexpr int k_snowflake_count = 1024 * 4;

namespace Hack {
struct SnowClock : public Hack::Base {
	SDL_Surface* fb;
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

		void simulate(bool drop_bottom) {
			// The bottom row of snow usually completely static once formed, but
			// when drop_bottom is true, we let it fall away.
			int start_y = h_ - (drop_bottom ? 1 : 2);
			// We continue once *something* has happened to the snow here, so it
			// only gets one change per tick.
			for(int y = start_y; y >= 0; --y) { // bottom-up makes falling natural
				for(int x = 0; x < w_; ++x) {
					Uint8& here = at(x, y);
					if(here > 0) {
						// Fall check
						// (An alternative would be to respawn them as flakes)
						Uint8& down = at(x, y+1);
						if(down < here) {
							flow(here, down);
							continue;
						}

						// Angle of repose check, must be away from walls
						// FIXME The left->right sweep means we spill left-biased anyway
						if(x > 0 && x < w_-1) {
							Uint8& down_left = at(x-1, y+1);
							Uint8& down_right = at(x+1, y+1);
							if(down_left < here) {
								if(down_right < here) {
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
							} else if (down_right < here) {
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

	class DigitalClock {
		struct Digit {
			bool segment[7];
			void number(int n) {
				segment[0] = // top
					n==0 || n==2 || n==3 || n==5 || n==6 || n==7 || n==8 || n==9;
				segment[1] = // top-left
					n==0 || n==4 || n==5 || n==6 || n==7 || n==8 || n==9;
				segment[2] = // top-right
					n==0 || n==1 || n==2 || n==3 || n==4 || n==7 || n==8 || n==9;
				segment[3] = // middle
					n==2 || n==3 || n==4 || n==5 || n==6 || n==8 || n==9;
				segment[4] = // bottom-left
					n==0 || n==2 || n==6 || n==8;
				segment[5] = // bottom-right
					n==0 || n==1 || n==3 || n==4 || n==5 || n==6 || n==7 || n==8 || n==9;
				segment[6] = // bottom
					n==0 || n==2 || n==3 || n==5 || n==6 || n==8 || n==9;
			}

			// sw and sh are *segment* height and width; st segment thickness.
			// Total render dimensions will be (sw, sh+st) due to the midline.
			void render(SDL_Surface* fb,
				Sint16 x, Sint16 y, Uint16 sw, Uint16 sh, Uint16 st) {
				for(int s = 0; s < 7; ++s) {
					if(!segment[s]) { continue; }
					SDL_Rect rect = { x, y, st, st };
					if(s==0||s==3||s==6) { // horizontal
						rect.x += st;
						rect.w = sw-(st*2);
					} else { // vertical
						rect.y += st;
						rect.h = sh-st;
					}
					if(s==2||s==5) { // right
						rect.x += sw-st;
					}
					if(s==4||s==5) { // bottom vertical
						rect.y += sh;
					}
					if(s==3) { // middle
						rect.y += sh;
					}
					if(s==6) { // bottom
						rect.y += sh*2;
					}
					SDL_FillRect(fb, &rect, 1);
				}
			}
		};
		Digit digits[4];
		int last_minute_;
		std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> fb;
	public:
		DigitalClock(int w, int h) :
			last_minute_(-1), fb(nullptr, SDL_FreeSurface) {
			// Make a framebuffer for the clock graphics, which can also be read
			// back for its physics. Only uses two colors.
			fb.reset(SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_ASYNCBLIT,
				w, h, 8, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000));
			if(fb.get() == nullptr) { throw std::bad_alloc(); }
			SDL_Color pal[] = {{0, 0, 0, 0}, {0, 255, 0, 0}};
			if(SDL_SetColors(fb.get(), pal, 0, 2) != 1) {
				throw std::runtime_error("failed to set clock palette");
			}
			if(SDL_SetColorKey(fb.get(), SDL_SRCCOLORKEY | SDL_RLEACCEL, 0) != 0) {
				throw std::runtime_error("failed to set color key");
			}
		}
		
		void set_time(const std::tm* tm) {
			// This is an optimization to avoid recalculating the same time each
			// tick, which assumes we'll never jump to the same minute in some
			// other time, which should be reasonable for a clock.
			if(last_minute_ == tm->tm_min) { return; }
			digits[0].number(tm->tm_hour / 10);
			digits[1].number(tm->tm_hour % 10);
			digits[2].number(tm->tm_min / 10);
			digits[3].number(tm->tm_min % 10);
			last_minute_ = tm->tm_min;

			// Render the segments to fb
			// Spacings as even divisions of width, where digits are double-wide:
			// gap, 2*digit, gap, 2*digit, colon, 2*digit, gap 2*digit, gap = 13
			// For height, it's 2*gap, 3*digit, 2*gap = 7
			SDL_FillRect(fb.get(), nullptr, 0);
			const int st = 8;
			int w = fb.get()->w;
			int h = fb.get()->h;
			int y = ((2*h) / 7) - (st/2); // centering correction
			int sw = (2*w) / 13;
			int sh = (3*h) / 14; // i.e. 1.5 sevenths
			for(int i=0; i<4; ++i) {
				digits[i].render(fb.get(), (((i*3)+1)*w)/13, y, sw, sh, st);
			}
		};

		SDL_Surface* rendered() { return fb.get(); } // treat as const
	};
	DigitalClock digital_clock;

	SnowClock(SDL_Surface* framebuffer)
		: fb(framebuffer),
		snowfb(nullptr, SDL_FreeSurface),
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
		static_snow(framebuffer->w, framebuffer->h),
		digital_clock(framebuffer->w, framebuffer->h) {

		/* Making SDL format-convert means we don't have to at write time, and
		 * can just slap down 32-bit values. */
		snowfb.reset(SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_ASYNCBLIT,
			fb->w, fb->h, 8, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000));
		if(snowfb.get() == nullptr) {
			throw std::bad_alloc();
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
		// FIXME migrate fixes from snowint
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
				if (breeze_delay[y] > 100) {
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
			if(flake.y >= 0 && flake.y < fb->h &&
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
			if(flake.x < 0) { flake.x += fb->w; }
			if(flake.x >= fb->w) { flake.x -= fb->w; }

			// Collide and collect with static snow/bottom of screen
			if(flake.y >= fb->h) {
				int mass = static_snow.at(flake.x, fb->h-1) + flake.mass;
				if(mass > 255) {
					static_snow.at(flake.x, fb->h-2) = mass - 255;
					mass = 255;
				}
				static_snow.at(flake.x, fb->h-1) = mass;
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
			}
		}
		// Simulate the static snow
		// Drop out on the hour for 15 seconds.
		static_snow.simulate(now->tm_min == 00 && now->tm_sec < 15);
		++tick;
	}
	void render() override {
		// Dirty regions only work if we can unpaint previous snowflake
		// positions, but separate simulate() makes that hard.
		int w = snowfb->w;
		int h = snowfb->h;
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
			// Skip out of bounds.
			if(flake.x < 0 || flake.x >= w
				|| flake.y < 0 || flake.y >= h)
				{ continue; }
			unsigned int bright = std::min(255u,
				flake.mass +  *pixel_at(flake.x, flake.y));
			*pixel_at(flake.x, flake.y) = bright;
		}

		if(SDL_MUSTLOCK(snowfb.get())) { SDL_UnlockSurface(snowfb.get()); }
		SDL_BlitSurface(snowfb.get(), nullptr, fb, nullptr);
		// Merge in the digital clock, which is color-keyed for transparency.
		SDL_BlitSurface(digital_clock.rendered(), nullptr, fb, nullptr);
		SDL_Flip(fb);
	}

	Uint32 tick_duration() override { return 100; } // 10Hz
};

std::unique_ptr<Hack::Base> MakeSnowClock(SDL_Surface* framebuffer) {
	return std::make_unique<SnowClock>(framebuffer);
}

}; // namespace Hack
