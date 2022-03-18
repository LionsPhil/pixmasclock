/* A digital clock that bursts into particles.
 * This is built upon (but does not inherit code in any clever way from) the
 * snow clock, but has greatly simplified particle behaviour, and static
 * particles instead re-explode into dynamic ones to move.
 */

#include <cassert>
#include <cmath>
#include <ctime>

#include <array>
#include <functional>
#include <random>
#include <stdexcept>
#include <vector>

#include <SDL.h>

#include "hack.hpp"

constexpr int k_particle_max = 1024 * 4;
constexpr double k_segment_drip_chance = 0.05;

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

		static constexpr double k_gravity = 0.02;
		static constexpr double k_friction = 0.8;
		static constexpr double k_elasticity = 0.5;
		static constexpr double k_movement_epsilon = 0.1;

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
	std::array<Particle, k_particle_max> particles;
	/* Find the index of the next free (inactive) particle in the particles
	 * array, or -1 if there are no free particles. Treats it in a circular
	 * buffer-ish fashion to avoid repeated O(N) sweeps. */
	int find_free_particle() {
		static int last_free_particle = 0;
		int found = -1;
		for(int i = last_free_particle; i < k_particle_max; ++i) {
			if(!particles[i].active) { found = i; break; }
		}
		if(found == -1) {
			for(int i = 0; i < last_free_particle; ++i) {
				if(!particles[i].active) { found = i; break; }
			}
		}
		if(found != -1) { last_free_particle = found + 1; }
		return found;
	}

	class StaticParticles {
		std::vector<Uint32> color_; // partfb format, i.e. ARGB; 0 = empty.
		int w_, h_;
		/* Dummy value for silenly allowing accesses outside the bounds, instead
		 * of needing lots of perfect defensive coding when looking at adjacent
		 * pixels (that will have to invent a value on read anyway). This isn't
		 * threadsafe, but we're not threaded. */
		Uint32 out_of_bounds_;

		// Convert to a dynamic particle, if there is one free, and clear the
		// static mass here if so (assuming here is a reference from at()).
		// Returns the index of the new particle (or -1 if failed).
		int try_pop(PopClock& h, int x, int y, Uint32& here) {
			int i = h.find_free_particle();
			if(i >= 0) {
				h.particles[i].pop(h, x, y, here);
				// Force downward momentum in every case.
				h.particles[i].dy = 1;
				here = 0;
			}
			return i;
		}

		// This is mostly split out for profiling reasons (when not inline).
		// If it's being called, "here" is nonzero.
		inline void simulate_one(PopClock& h,
			std::function<bool(int,int)> obstacles,
			int x, int y, Uint32& here) {
			// Hit check; get crushed by obstacles
			if(obstacles(x, y)) { here = 0; }

			// Fall check
			Uint32& down = at(x, y+1);
			if((down == 0) && !obstacles(x, y+1)) {
				int i = try_pop(h, x, y, here);
				if(i > 0) {
					// No horizontal movement.
					h.particles[i].dx = 0;
				}
				return;
			}

			// Angle of repose check, must be away from walls
			// FIXME The left->right sweep means we spill left-biased anyway
			if(x > 0 && x < w_-1) {
				Uint32& down_left = at(x-1, y+1);
				bool down_left_obstacle = obstacles(x-1, y+1);
				Uint32& down_right = at(x+1, y+1);
				bool down_right_obstacle = obstacles(x+1, y+1);
				if(down_left == 0 && ! down_left_obstacle) {
					if(down_right == 0 && !down_right_obstacle) {
						// Split, 3-way flow. Go either way!
						try_pop(h, x, y, here);
					} else {
						// Spill left
						int i = try_pop(h, x, y, here);
						if(i >= 0) { h.particles[i].dx = -1; }
					}
					return;
				} else if (down_right == 0 &&
					!down_right_obstacle) {
					// Spill right
					int i = try_pop(h, x, y, here);
					if(i >= 0) { h.particles[i].dx = 1; }
					return;
				}
			}
		}

		inline Uint32& unsafe_at(int x, int y) {
			return color_[x + (y*w_)];
		}

	public:
		StaticParticles(int w, int h) : w_(w), h_(h) {
			color_.resize(w_ * h_);
		}

		Uint32& at(int x, int y) {
			if(x < 0 || x >= w_ || y < 0 || y >= h_) {
				out_of_bounds_ = 0; // In case garbage was written previously.
				return out_of_bounds_;
			} else {
				// Since we've done our own bounds check, and we size this
				// precisely once in the c'tor and *shouldn't* screw that up,
				// use unsafe operator[] instead of at() to skip doing it again.
				return unsafe_at(x, y);
			}
		}

		void simulate(PopClock& h, bool drop_bottom,
			std::function<bool(int,int)> obstacles) {
			// The bottom row is usually completely static once formed, but
			// when drop_bottom is true, we let it fall away.
			int start_y = h_ - (drop_bottom ? 1 : 2);
			// We continue once *something* has happened to the mass here, so it
			// only gets one change per tick.
			// Bottom-up makes falling natural.
			for(int y = start_y; y >= 0; --y) {
				for(int x = 0; x < w_; ++x) {
					Uint32& here = unsafe_at(x, y); // We're iterating in-bounds
					if(here > 0) {
						simulate_one(h, obstacles, x, y, here);
					}
				}
			}
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
			s += 60 * (tm->tm_min % 3); // three minute hue cycle
			hue_to_rgb(s/(60.0 * 3), r, g, b);
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
			assert(x >= 0); assert(x <= buffer->w);
			assert(y >= 0); assert(y <= buffer->h);
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
		// Get localtime and set the clock.
		std::time_t now_epoch = std::time(nullptr);
		std::tm* now = std::localtime(&now_epoch);
		digital_clock.set_time(now);

		// Perhaps spawn some particles dripping/launching off of segments.
		SDL_Color& color_struct =
			digital_clock.rendered()->format->palette->colors[1];
		Uint32 color = SDL_MapRGB(partfb.get()->format,
			color_struct.r, color_struct.g, color_struct.b);
		for(int d = 0; d < 4; ++d) {
			auto digit = digital_clock.get_digit(d);
			for(int segment = 0; segment < 7; ++segment) {
				if(digit.segment[segment] &&
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
					if(static_particles.at(x, y) == 0) {
						int i = find_free_particle();
						if(i >= 0) {
							particles[i].pop(*this, x, y, color);
							particles[i].dy = abs(particles[i].dy);
							if(!drip) {
								particles[i].dy *= -1;
							}
						}
					}
				}
			}
		}

		// Simulate particles.
		for(auto&& particle : particles) {
			if(!particle.active) { continue; }
			if(!particle.simulate(
				// The floor must always be solid to avoid travel out of bounds.
				[&](auto x, auto y) {
					return
						x < 0 || x >= fb->w ||
						y < 0 || y >= fb->h ||
						static_particles.at(x, y) != 0 ||
						digital_clock.solid_at(x, y);
				})) {
				// Move this particle to the static layer.
				static_particles.at(particle.x, particle.y) = particle.color;
				particle.stop();
			}
		}

		// Simulate the static particle mass.
		// Drop out on the hour for 15 seconds.
		// Nasty hack: this is really expensive, and best I can tell from the
		// profiling, it's the big sweep through static particle memory
		// assessing every pixel in the first place, regardless of it we act
		// on them. It does suck. So hack: do it every *other* tick.
		bool dropout = now->tm_min == 0 && now->tm_sec < 15;
		static Uint8 tick;
		if(dropout || (++tick % 2 == 0)) {
			static_particles.simulate(*this, dropout,
				[&](auto x, auto y){return digital_clock.solid_at(x, y);});
		}
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
				if(static_particles.at(x, y) != 0) {
					*pixel_at(x, y) = static_particles.at(x, y);
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
