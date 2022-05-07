/* A digital clock that bursts into particles.
 * This is built upon (but does not inherit code in any clever way from) the
 * snow clock, but has greatly simplified particle behaviour, and static
 * particles instead re-explode into dynamic ones to move.
 */

#include <cassert>
#include <cmath>
#include <ctime>

#include <algorithm>
#include <functional>
#include <random>
#include <stdexcept>
#include <vector>

#include <SDL.h>

#include "hack.hpp"

constexpr size_t k_defragment_threshold = 2048; // Don't defrag to < this.
constexpr int k_defragment_factor = 2; // N times size vs number active.
constexpr double k_segment_drip_chance = 0.075;
constexpr int k_hue_rotation_minutes = 30;
constexpr bool k_digits_drip = false;
constexpr bool k_digits_pop = true;
constexpr bool k_explode_on_hour = true;
constexpr bool k_debug_fastclock = true;

namespace Hack {
struct PopClock : public Hack::Base {
	SDL_Surface* fb;
	// Build up the particles for buffering, and also we want
	// to write raw in a known pixel format rather than FillRect.
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> partfb;
	std::default_random_engine generator;
	std::uniform_int_distribution<int> random_coinflip;
	std::uniform_real_distribution<double> random_frac;

	struct Particle {
		bool active;
		double x, y, dx, dy; // dx/dy should not exceed one.
		double tv; // terminal velocity can be *less* than one.
		Uint32 color; // Same format as partfb, i.e. ARGB.

		static constexpr double k_gravity = 0.01;
		static constexpr double k_friction = 0.8;
		static constexpr double k_elasticity = 0.5;
		static constexpr double k_movement_epsilon = 0.1;

		Particle() : active(false) {}

		// Explode alive with random movement.
		void pop(PopClock& h, double x, double y, Uint32 c) {
			active = true;
			this->x = x;
			this->y = y;
			tv = (h.random_frac(h.generator) * 0.7) + 0.3;
			dx = (h.random_frac(h.generator) * tv);
			if(h.random_coinflip(h.generator)) { dx *= -1.0; }
			dy = (h.random_frac(h.generator) * tv);
			if(h.random_coinflip(h.generator)) { dy *= -1.0; }
			color = c;
		}

		// Stop and free up to be reused for another particle.
		void stop() {
			active = false;
		}

		// If returns false, the particle has settled and should switch to the
		// static layer.
		bool simulate(std::function<bool(int,int)> obstacles) {
			assert(active);

			// Work out potential new location (prime).
			double xp = x + dx;
			double yp = y + dy;

			if(obstacles(xp, yp)) {
				// We would hit something; bounce instead.
				if(obstacles(xp, y)) { // Colliding horizontally.
					dx *= -k_elasticity;
					xp = x;
				}
				if(obstacles(x, yp)) { // Colliding vertically.
					dy *= -k_elasticity;
					dx *= k_friction; // Don't slide along the bottom freely.
					yp = y;
				}
			}
			// Move to new space
			x = xp; y = yp;
			// Accellerate due to gravity up to terminal.
			dy = std::min(tv, dy + k_gravity);
			// Staticize fully stationary particles unless they can fall.
			return (abs(dx) > k_movement_epsilon ||
				abs(dy) > k_movement_epsilon ||
				!obstacles(x, y+1));
		}
	};
	std::vector<Particle> particles;

	/* Get an index for the next free (inactive) particle in the particles
	 * vector. In past versions this did clever circular buffer stuff with
	 * a static-sized array. Now we just throw it at vector to deal with.
	 * Can no longer return -1 for no free particles. Return is always valid. */
	size_t find_free_particle() {
		particles.emplace_back();
		return particles.size() - 1;
	}

	void defragment_particles() {
		// Note this doesn't touch the allocation; that's up to vector, and
		// since we're not hurting for memory there's not much reason to be
		// reallocating. We're reducing the logical size so we can iterate over
		// less, so we may as well make it tight for now.
		particles.erase(
			std::remove_if(particles.begin(), particles.end(),
				[](Particle& p){ return !p.active; }),
			particles.end()
		);
	}

	class StaticParticles {
		std::vector<Uint32> color_; // partfb format, i.e. ARGB; 0 = empty.
		int w_, h_;
		// Y co-ordinate of higest particle needing simulation (h = none).
		int needs_sim_up_to;

		// Convert to a dynamic particle, if there is one free, and clear the
		// static mass here if so.
		// Returns the index of the new particle (or -1 if failed).
		int try_pop(PopClock& h, int x, int y, Uint32 here, bool down=true) {
			size_t i = h.find_free_particle();
			h.particles[i].pop(h, x, y, here);
			// Force downward momentum.
			if(down) { h.particles[i].dy = abs(h.particles[i].dy); }
			set(x, y, 0);
			return i;
		}

		// This is mostly split out for profiling reasons (when not inline).
		// If it's being called, "here" is nonzero.
		inline void simulate_one(PopClock& h,
			std::function<bool(int,int)> obstacles, bool drop_bottom,
			int x, int y, Uint32 here) {
			// Hit check; get crushed by obstacles
			if(obstacles(x, y)) { set(x, y, 0); return; }

			// Fall check
			bool fall = false;
			if(y+1 >= h_) {
				if(drop_bottom) { fall = true; }
			} else {
				Uint32 down = get(x, y+1);
				if((down == 0) && !obstacles(x, y+1)) { fall = true; }
			}
			if(fall) {
				int i = try_pop(h, x, y, here);
				if(i > 0) {
					// Damped horizontal movement.
					h.particles[i].dx *= 0.25;
				}
				return;
			}
			// We shouldn't be simming the bottom row beyond this point!
			// That would mean we got run on it without drop_bottom set, which
			// would be, at best, pointless. But also means we're confused.
			// (And we will throw on the assert in obstacles() checks below.)
			assert(y+1 < h_);

			// Angle of repose check
			// FIXME The left->right sweep means we spill left-biased anyway
			Uint32 down_left = get(x-1, y+1);
			bool down_left_obstacle = x==0 ? true : obstacles(x-1, y+1);
			Uint32 down_right = get(x+1, y+1);
			bool down_right_obstacle = x==w_-1 ? true : obstacles(x+1, y+1);
			if(down_left == 0 && ! down_left_obstacle) {
				if(down_right == 0 && !down_right_obstacle) {
					// Split, 3-way flow. Go either way!
					try_pop(h, x, y, here);
					return;
				} else {
					// Spill left
					int i = try_pop(h, x, y, here);
					if(i >= 0)
						{ h.particles[i].dx = -abs(h.particles[i].dx); }
				}
				return;
			} else if (down_right == 0 &&
				!down_right_obstacle) {
				// Spill right
				int i = try_pop(h, x, y, here);
				if(i >= 0)
					{ h.particles[i].dx = abs(h.particles[i].dx); }
				return;
			}
		}

		inline Uint32& unsafe_at(int x, int y) {
			return color_[x + (y*w_)];
		}

	public:
		StaticParticles(int w, int h) : w_(w), h_(h), needs_sim_up_to(h) {
			color_.resize(w_ * h_);
		}

		Uint32 get(int x, int y) {
			if(x < 0 || x >= w_ || y < 0 || y >= h_) { return 0; }
			return unsafe_at(x, y);
		}

		void set(int x, int y, Uint32 c) {
			if(x < 0 || x >= w_ || y < 0 || y >= h_) { return; }
			unsafe_at(x, y) = c;
			// Allow for the one above us to fall.
			needs_sim_up_to = std::min(needs_sim_up_to, std::max(0, y - 1));
		}

		void simulate(PopClock& h, bool drop_bottom,
			std::function<bool(int,int)> obstacles) {
			// The bottom row is usually completely static once formed, but
			// when drop_bottom is true, we let it fall away.
			int start_y = h_ - (drop_bottom ? 1 : 2);
			// Only sim up to changes; if drop-bottom, that forces bottom row.
			// (If not drop-bottom, if nothing else active, don't loop at all.)
			int stop_y = std::min(needs_sim_up_to, drop_bottom ? h_ - 1 : h_);
			needs_sim_up_to = h_;
			// We continue once *something* has happened to the mass here, so it
			// only gets one change per tick.
			// Bottom-up makes falling natural.
			for(int y = start_y; y >= stop_y; --y) {
				for(int x = 0; x < w_; ++x) {
					Uint32 here = unsafe_at(x, y); // We're iterating in-bounds
					if(here > 0) {
						simulate_one(h, obstacles, drop_bottom, x, y, here);
					}
				}
			}
		}

		void force_full_simulate_next(int up_to) {
			needs_sim_up_to = up_to;
		}

		void pop_all(PopClock& h) {
			for(int y = 0; y < h_; ++y) {
				for(int x = 0; x < w_; ++x) {
					Uint32 here = unsafe_at(x, y); // We're iterating in-bounds
					if(here > 0) {
						try_pop(h, x, y, here, false);
					}
				}
			}
			// Cancel all sim; we've just wiped all static particles away.
			needs_sim_up_to = h_;
		}
	};
	StaticParticles static_particles;

	class DigitalClock {
		struct Digit {
			bool segment[7];
			SDL_Rect segrect[7];

			Digit() {
				for(int s = 0; s < 7; ++s) {
					segment[s] = false;
					segrect[s] = { 0, 0, 0, 0 };
				}
			}

			// sw and sh are *segment* height and width; st segment thickness.
			// Total render dimensions will be (sw, sh+st) due to the midline.
			void size_for(Sint16 x, Sint16 y, Uint16 sw, Uint16 sh, Uint16 st) {
				for(int s = 0; s < 7; ++s) {
					segrect[s] = { x, y, st, st };
					if(s==0||s==3||s==6) { // horizontal
						segrect[s].x += st;
						segrect[s].w = sw-(st*2);
					} else { // vertical
						segrect[s].y += st;
						segrect[s].h = sh-st;
					}
					if(s==2||s==5) { // right
						segrect[s].x += sw-st;
					}
					if(s==4||s==5) { // bottom vertical
						segrect[s].y += sh;
					}
					if(s==3) { // middle
						segrect[s].y += sh;
					}
					if(s==6) { // bottom
						segrect[s].y += sh*2;
					}
				}
			}

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

			void render(SDL_Surface* fb) {
				for(int s = 0; s < 7; ++s) {
					if(segment[s]) { SDL_FillRect(fb, &segrect[s], 1); }
				}
			}
		};
		Digit digits[4];
		int last_minute_;
		int last_second_;
		std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> fb;
		// cache format info
		Uint8 bytes_per_pixel_;
		Uint16 pitch_;
	public:
		DigitalClock(int w, int h) :
			last_minute_(-1), last_second_(-1), fb(nullptr, SDL_FreeSurface) {
			fb.reset(make_surface(w, h));

			// Spacings as even divisions of width, where digits are double-wide:
			// gap, 2*digit, gap, 2*digit, colon, 2*digit, gap 2*digit, gap = 13
			// For height, it's 2*gap, 3*digit, 2*gap = 7
			SDL_FillRect(fb.get(), nullptr, 0);
			const int st = 8;
			int y = ((2*h) / 7) - (st/2); // centering correction
			int sw = (2*w) / 13;
			int sh = (3*h) / 14; // i.e. 1.5 sevenths
			for(int i = 0; i < 4; ++i) {
				digits[i].size_for((((i*3)+1)*w)/13, y, sw, sh, st);
			}
		}

		// Make a framebuffer for the clock graphics, which can also be read
		// back for its physics (but that was a bad idea). Only uses two colors.
		// The clock does this automatically for its own internal surface.
		SDL_Surface* make_surface(int w, int h) {
			std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> s(
				SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_ASYNCBLIT,
					w, h, 8, 0x00ff0000, 0x0000ff00, 0x000000ff, 0),
				SDL_FreeSurface);
			if(s.get() == nullptr) { throw std::bad_alloc(); }
			if(SDL_MUSTLOCK(s.get())) {
				// This is bad, because solid_at will randomly fail (SDL is
				// allowed to make the pixels member of lockable surfaces
				// NULL when it feels like it if it's not locked).
				throw std::runtime_error("clock surface requires locking");
			}
			bytes_per_pixel_ = s.get()->format->BytesPerPixel;
			pitch_ = s.get()->pitch;
			// Third palette entry is for stupid debugging tricks.
			SDL_Color pal[] = {{0, 0, 0, 0}, {0, 255, 0, 0}, {0, 127, 255, 0}};
			if(SDL_SetColors(s.get(), pal, 0, 3) != 1) {
				throw std::runtime_error("failed to set clock palette");
			}
			/* Can't use RLEACCEL while still doing pixels evil:
			 * https://discourse.libsdl.org/t/pixels-getting-set-to-null/9811/10
			 * But we're not any more, whee!
			 */
			if(SDL_SetColorKey(s.get(), SDL_SRCCOLORKEY|SDL_RLEACCEL, 0) != 0) {
				throw std::runtime_error("failed to set color key");
			}
			return s.release();
		}

		// Do a big dirty sigmoid function hack to make hues more red.
		// Hand-tuned constants to get *approximately* [0,1]->[0,1] ranges,
		// although strictly sigmoid is [-inf,inf]->[0,1].
		// It's too aggressive, though.
		double big_dirty_sigmoid(double x) {
			// In Wolfram Alpha-ese:
			// y=Divide[1,1+Power[4,-8\(40)x-0.5\(41)]]
			return 1.0 / (1.0 + pow(4.0, -8.0 * (x - 0.5)));
		}

		// This is better but ultimately I preferred leaving the hue alone.
		double big_dirty_sin(double x) {
			return 0.5 + 0.5 * (sin(M_PI * (x - 0.5)));
		}

		void hue_to_rgb(double h, Uint8& out_r, Uint8& out_g, Uint8& out_b) {
			double r = 0, g = 0, b = 0;
			// https://www.rapidtables.com/convert/color/hsv-to-rgb.html
			double x = 1.0 - abs(fmod(h * 6.0, 2.0) - 1.0);
			if(h < 1.0/6.0) {
				r = 1.0; g = x;
			} else if(h < 2.0/6.0) {
				r = x; g = 1.0;
			} else if(h < 3.0/6.0) {
				g = 1.0; b = x;
			} else if(h < 4.0/6.0) {
				g = x; b = 1.0;
			} else if(h < 5.0/6.0) {
				r = x; b = 1.0;
			} else {
				r = 1.0; b = x;
			}
			out_r = std::min(255.0, (255 * r) + (64 * b));
			out_g = std::min(255.0, (191 * g) + (64 * b));
			out_b = 255 * b;
		}

		// Returns true if solid regions have changed.
		bool set_time(const std::tm* tm) {
			// This is an optimization to avoid recalculating the same time each
			// tick, which assumes we'll never jump to the same second in some
			// other time, which should be reasonable for a clock.
			if(last_second_ == tm->tm_sec) { return false; }
			last_second_ = tm->tm_sec;

			// Change the rainbow hue based on the second.
			Uint8 r, g, b; Uint32 s;
			s = std::min(tm->tm_sec, 59); // no doing evil with leap seconds
			s += 60 * (tm->tm_min % k_hue_rotation_minutes);
			hue_to_rgb(s/(60.0 * k_hue_rotation_minutes), r, g, b);
			SDL_Color pal[] = {{r, g, b, 0}};
			if(SDL_SetColors(fb.get(), pal, 1, 1) != 1) {
				throw std::runtime_error("failed to set clock palette");
			}

			// The actually rendering is only every minute.
			if(last_minute_ == tm->tm_min) { return false; }
			last_minute_ = tm->tm_min;
			digits[0].number(tm->tm_hour / 10);
			digits[1].number(tm->tm_hour % 10);
			digits[2].number(tm->tm_min / 10);
			digits[3].number(tm->tm_min % 10);

			// Render the segments to fb
			SDL_FillRect(fb.get(), nullptr, 0);
			for(int i=0; i<4; ++i) {
				digits[i].render(fb.get());
			}
			return true;
		};

		SDL_Surface* rendered() { return fb.get(); } // treat as const

		bool solid_at(int x, int y) {
			auto buffer = fb.get();
			assert(x >= 0); assert(x < buffer->w);
			assert(y >= 0); assert(y < buffer->h);
			return static_cast<Uint8 *>(buffer->pixels)
				[(x*bytes_per_pixel_)+(y*pitch_)] != 0;
		}

		Digit& get_digit(int i) { // treat as const
			assert(i >= 0); assert (i <= 4);
			return digits[i];
		}
	};
	DigitalClock digital_clock;

	PopClock(SDL_Surface* framebuffer)
		: fb(framebuffer),
		partfb(nullptr, SDL_FreeSurface),
		random_coinflip(0, 1),
		random_frac(0, 1),
		static_particles(framebuffer->w, framebuffer->h),
		digital_clock(framebuffer->w, framebuffer->h) {

		/* Making SDL format-convert means we don't have to at write time, and
		 * can just slap down 32-bit values. */
		partfb.reset(SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_ASYNCBLIT,
			fb->w, fb->h, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0));
		if(partfb.get() == nullptr) {
			throw std::bad_alloc();
		}

		for(auto&& particle : particles) {
			particle.stop();
		}
	}

	void simulate() override {
		auto w = fb->w, h = fb->h;
		// Get localtime and set the clock.
		std::time_t now_epoch = std::time(nullptr);
		std::tm* now = std::localtime(&now_epoch);
		if(k_debug_fastclock) {
			now->tm_hour = now->tm_min % 24;
			now->tm_min = now->tm_sec;
		}
		bool clock_changed = digital_clock.set_time(now);
		if(clock_changed) {
			// This is a bit cheeky, making assumptions about digit layout,
			// but saves us scanning the top chunk of the display for nothing.
			static_particles.force_full_simulate_next(
				digital_clock.get_digit(0).segrect[0].y - 1);
		}

		// Drop out on the hour for 15 seconds.
		bool dropout = now->tm_min == 0 && now->tm_sec < 15;

		if(k_explode_on_hour) {
			static int last_hour = -1;
			if(last_hour != now->tm_hour) {
				static_particles.pop_all(*this);
				last_hour = now->tm_hour;
			}
		}

		// Perhaps spawn some particles dripping/launching off of segments.
		SDL_Color& color_struct =
			digital_clock.rendered()->format->palette->colors[1];
		Uint32 color = SDL_MapRGB(partfb.get()->format,
			color_struct.r, color_struct.g, color_struct.b);
		for(int d = 0; d < 4; ++d) {
			auto digit = digital_clock.get_digit(d);
			for(int segment = 0; segment < 7; ++segment) {
				bool present = digit.segment[segment];
				// Drip from existing segments.
				if(k_digits_drip && present &&
					random_frac(generator) < k_segment_drip_chance) {

					bool drip = random_coinflip(generator);
					int x = digit.segrect[segment].x;
					x += random_frac(generator) * digit.segrect[segment].w;
					int y = digit.segrect[segment].y;
					if(drip) {
						y += digit.segrect[segment].h;
					} else {
						--y;
					}
					if(static_particles.get(x, y) == 0) {
						size_t i = find_free_particle();
						particles[i].pop(*this, x, y, color);
						particles[i].dy = abs(particles[i].dy);
						if(!drip) {
							particles[i].dy *= -1;
						}
					}
				}
				// Pop from freshly missing segments.
				if(k_digits_pop && clock_changed) {
					static bool previous_segments[4][7];
					if(!present && previous_segments[d][segment]) {
						// This segment just vanished; pop it.
						Sint16 x = digit.segrect[segment].x;
						Sint16 y = digit.segrect[segment].y;
						for(Uint16 yo=0; yo < digit.segrect[segment].h; ++yo) {
							for(Uint16 xo=0; xo < digit.segrect[segment].w;
								++xo) {
								size_t i = find_free_particle();
								particles[i].pop(*this, x+xo, y+yo, color);
								particles[i].dy = -abs(particles[i].dy);
							}
						}
					}
					previous_segments[d][segment] = present;
				}
			}
		}

		// Simulate particles.
		size_t active_particles = 0;
		for(auto&& particle : particles) {
			if(!particle.active) { continue; }
			++active_particles;
			if(!particle.simulate(
				// The floor must always be solid to avoid travel out of bounds
				// ...except we break that rule during dropout and catch it
				// below. We still need to not doing solid_at() checks OOB.
				[&](auto x, auto y) {
					if(dropout && y >= h) { return false; }
					return
						x < 0 || x >= w ||
						y < 0 || y >= h ||
						static_particles.get(x, y) != 0 ||
						digital_clock.solid_at(x, y);
				})) {
				// Move this particle to the static layer.
				static_particles.set(particle.x, particle.y, particle.color);
				particle.stop();
			}
			if(dropout && particle.y >= h) {
				// We've let this particle fall out of bounds, and *must* now
				// stop it since that's invalid and will crash during render.
				particle.stop();
			}
		}

		// Defragment particles if it's getting sparse.
		if((particles.size() > k_defragment_threshold) &&
			((active_particles * k_defragment_factor) < particles.size())) {
			defragment_particles();
		}

		// Simulate the static particle mass.
		static_particles.simulate(*this, dropout,
			[&](auto x, auto y){return digital_clock.solid_at(x, y);});
	}

	void render() override {
		int w = partfb->w;
		int h = partfb->h;
		if(SDL_MUSTLOCK(partfb.get())) { SDL_LockSurface(partfb.get()); }
		SDL_FillRect(partfb.get(), nullptr, 0);
		Uint8* pfb_pixels = reinterpret_cast<Uint8*>(partfb.get()->pixels);
		auto bytes_per_pixel = partfb->format->BytesPerPixel;
		auto pitch = partfb->pitch;
		auto pixel_at = [&](Sint16 x, Sint16 y){
			return reinterpret_cast<Uint32*>(
				pfb_pixels + (x*bytes_per_pixel) + (y*pitch));
		};

		for(Sint16 y=0; y<h; ++y) {
			for(Sint16 x=0; x<w; ++x) {
				Uint32 c = static_particles.get(x, y);
				if(c != 0) {
					*pixel_at(x, y) = c;
				}
			}
		}

		for(auto&& particle : particles) {
			if(!particle.active) { continue; }
			*pixel_at(particle.x, particle.y) = particle.color;
		}

		if(SDL_MUSTLOCK(partfb.get())) { SDL_UnlockSurface(partfb.get()); }
		SDL_BlitSurface(partfb.get(), nullptr, fb, nullptr);
		// Merge in the digital clock, which is color-keyed for transparency.
		SDL_BlitSurface(digital_clock.rendered(), nullptr, fb, nullptr);
		//SDL_BlitSurface(prev_clock.get(), nullptr, fb, nullptr); // DEBUG
		SDL_Flip(fb);
	}

	Uint32 tick_duration() override { return 33; } // 30Hz
};

std::unique_ptr<Hack::Base> MakePopClock(SDL_Surface* framebuffer) {
	return std::make_unique<PopClock>(framebuffer);
}

}; // namespace Hack
