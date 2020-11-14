// Blah blah Philip Boulain blah copyright blah 2020 blah 3-CLAUSE BSD LICENSE
#include <cassert>
#include <cmath>

#include <array>
#include <exception>
#include <memory>
#include <random>
#include <vector>

#include <SDL.h>

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
				0, SDL_SWSURFACE | SDL_DOUBLEBUF);
			if(framebuffer == nullptr) { throw Error(); }
		}
		~Graphics() {
			SDL_Quit();
		}
	};
};

// Graphical hack interface ---------------------------------------------------
// (yeah yeah separate files, I CBA to wrassle VScode vs Makefiles right now)

namespace Hack {
struct Base {
	virtual ~Base() {}
	virtual void simulate() = 0;
	virtual void render() = 0;
	virtual Uint32 tick_duration() = 0;
};

constexpr int k_drifting_snowflake_count = 1024;
struct DriftingSnow : public Hack::Base {
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
	std::array<Snowflake, k_drifting_snowflake_count> snowflakes;

	std::vector<double> breezes;

	DriftingSnow(SDL_Surface* framebuffer)
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

	void simulate() override {
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
	void render() override {
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
		SDL_Flip(fb);
	}

	Uint32 tick_duration() override { return 100; } // 10Hz
};

// This one also expunges the floating point from DriftingSnow
constexpr int k_physical_snowflake_count = 4096;
struct PhysicalSnow : public Hack::Base {
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
	std::vector<Uint8> static_snow;

	struct Snowflake {
		Sint16 x, y, dx; // dx is sign only
		// Inverse of dx/dy, i.e. how many ticks between each step.
		// delay_t is the terminal velocity, the smallest delays can get.
		unsigned int delay_x, delay_y, delay_t;
		unsigned int mass;

		void init(PhysicalSnow& h) {
			reset_common(h);
			y = h.random_y(h.generator);
			delay_y = h.random_delay_y(h.generator);
		}

		void reset_at_top(PhysicalSnow& h) {
			reset_common(h);
			y = 0;
			// Stop things getting too lockstep.
			delay_y /= 2;
			delay_y += 1 + (h.random_delay_y(h.generator) / 2);
		}

	private:
		void reset_common(PhysicalSnow& h) {
			x = h.random_x(h.generator);
			dx = h.random_coinflip(h.generator) == 1 ? 1 : -1;
			delay_x = h.random_delay_x(h.generator);
			mass = h.random_mass(h.generator);
			delay_t = ((255-mass) / 25) + 1;
		}
	};
	std::array<Snowflake, k_physical_snowflake_count> snowflakes;

	PhysicalSnow(SDL_Surface* framebuffer)
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
		next_breeze_in(0),
		static_snow(framebuffer->w * framebuffer->h) {

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

#ifdef STATIC_SNOW
		int i = 0;
		for(Sint16 y=0; y<fb->h; ++y) {
			for(Sint16 x=0; x<fb->w; ++x) {
				++i;
				if(static_snow[i]>0) {
					SDL_Rect position { x, y, 1, 1};
					SDL_FillRect(fb, &position, greyscale[static_snow[i]]);
				}
			}
		}
#endif

		for(auto&& flake : snowflakes) {
			SDL_Rect position { flake.x, flake.y, 1, 1};
			// Skip out of bounds.
			if(position.x < 0 || position.x >= fb->w
				|| position.y < 0 || position.y >= fb->h)
				{ continue; }
#ifdef STATIC_SNOW
			unsigned int bright = std::min(255u,
				flake.mass + static_snow[ss_at(flake.x, flake.y)]);
#else
			unsigned int bright = flake.mass;
#endif
			SDL_FillRect(fb, &position, greyscale[bright]);
		}

		if(SDL_MUSTLOCK(fb)) { SDL_UnlockSurface(fb); }
		SDL_Flip(fb);
	}

	Uint32 tick_duration() override { return 100; } // 10Hz
};
}; // namespace Hack

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
	SDL_Flip(fb);

	std::unique_ptr<Hack::Base> hack;
	//hack.reset(new Hack::DriftingSnow(fb));
	hack.reset(new Hack::PhysicalSnow(fb));

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
			do {
				tickerror -= hack->tick_duration();
				hack->simulate();
			} while(tickerror >= hack->tick_duration());
			hack->render();
		} else {
			/// Have a nap until we actually have at least one tick to run.
			SDL_Delay(hack->tick_duration());
		}
	}

	return EXIT_SUCCESS;
}
