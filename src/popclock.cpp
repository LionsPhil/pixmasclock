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

#include "hack.hpp"
#include "digitalclock.hpp"

constexpr size_t k_defragment_threshold = 128; // Don't defrag to < this.
constexpr int k_defragment_factor = 2; // N times size vs number active.
constexpr double k_segment_drip_chance = 0.075;
constexpr bool k_digits_drip = false;
constexpr bool k_digits_pop = true;
constexpr bool k_explode_on_hour = true;
constexpr bool k_debug_fastclock = false;

namespace Hack {
struct PopClock : public Hack::Base {
	int w, h;
	// Build up the particles for buffering, and also we want
	// to write raw in a known pixel format rather than FillRect.
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> partfb;
	std::default_random_engine generator;
	std::uniform_int_distribution<int> random_coinflip;
	std::uniform_real_distribution<double> random_frac;
	bool needs_paint; // Something has changed to render.

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
			bool blocked_x = false;
			bool blocked_y = false;

			if(obstacles(xp, yp)) {
				// We would hit something; bounce instead.
				if(obstacles(xp, y)) { // Colliding horizontally.
					dx *= -k_elasticity;
					xp = x;
					blocked_x = true;
				}
				if(obstacles(x, yp)) { // Colliding vertically.
					dy *= -k_elasticity;
					dx *= k_friction; // Don't slide along the bottom freely.
					yp = y;
					blocked_y = true;
				}
			}
			// Move to new space
			x = xp; y = yp;
			// Particles are still alive if:
			//  - they have above-epsilon velocity
			bool moving =
				(abs(dx) > k_movement_epsilon) ||
				(abs(dy) > k_movement_epsilon);
			//  - they have open space below them to fall into; gravity should
			//    eventually win even if they're grinding on the X axis
			bool can_fall = !obstacles(x, y+1);
			//  - they aren't jammed into an obstacle such it's fully ignored
			bool making_progress = !blocked_x || !blocked_y;
			// Accellerate due to gravity up to terminal.
			dy = std::min(tv, dy + k_gravity);
			// And return the activity judgement.
			return (moving || can_fall) && making_progress;
		}
	};
	std::vector<Particle> particles;
	bool have_live_particles;

	/* Get an index for the next free (inactive) particle in the particles
	 * vector. In past versions this did clever circular buffer stuff with
	 * a static-sized array. Now we just throw it at vector to deal with.
	 * Can no longer return -1 for no free particles. Return is always valid. */
	size_t find_free_particle() {
		particles.emplace_back();
		have_live_particles = true;
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
		// Returns if it did anything
		inline bool simulate_one(PopClock& h,
			std::function<bool(int,int)> obstacles, bool drop_bottom,
			int x, int y, Uint32 here) {
			// Hit check; get crushed by obstacles
			if(obstacles(x, y)) { set(x, y, 0); return true; }

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
				// Damped horizontal movement.
				h.particles[i].dx *= 0.25;
				return true;
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
					return true;
				} else {
					// Spill left
					int i = try_pop(h, x, y, here);
					h.particles[i].dx = -abs(h.particles[i].dx);
				}
				return true;
			} else if (down_right == 0 &&
				!down_right_obstacle) {
				// Spill right
				int i = try_pop(h, x, y, here);
				h.particles[i].dx = abs(h.particles[i].dx);
				return true;
			}
			return false;
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

		bool simulate(PopClock& h, bool drop_bottom,
			std::function<bool(int,int)> obstacles) {
			bool done_something = false;
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
						done_something |=
							simulate_one(h, obstacles, drop_bottom, x, y, here);
					}
				}
			}
			return done_something;
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
	DigitalClock digital_clock;

	PopClock(int w, int h)
		: w(w), h(h),
		partfb(nullptr, SDL_FreeSurface),
		random_coinflip(0, 1),
		random_frac(0, 1),
		needs_paint(true),
		have_live_particles(false),
		static_particles(w, h),
		digital_clock(w, h, true) {

		/* Making SDL format-convert means we don't have to at write time, and
		 * can just slap down 32-bit values. */
		partfb.reset(SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_ASYNCBLIT,
			w, h, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0));
		if(partfb.get() == nullptr) {
			throw std::bad_alloc();
		}

		for(auto&& particle : particles) {
			particle.stop();
		}
	}

	void simulate() override {
		static int last_second;
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
			needs_paint = true;
		}
		if(last_second != now->tm_sec) {
			// Bit of an info leak that we know the clock makes quiet visual
			// changes every second (its palette), but not shape changes.
			needs_paint = true;
			last_second = now->tm_sec;
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
		if(have_live_particles) {
			for(auto&& particle : particles) {
				if(!particle.active) { continue; }
				++active_particles;
				if(!particle.simulate(
					// The floor must always be solid to avoid travel out of
					// bounds...except we break that rule during dropout and
					// catch it below. We still need to not do solid_at()
					// checks OOB.
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
					// We've let this particle fall out of bounds, and *must*
					// now stop it since that's invalid and will crash during
					// render.
					particle.stop();
				}
			}

			// Stop simulating particles on future ticks if we don't have any
			// active ones now. This will get reset by something using
			// find_free_particle() to generate a new one.
			if(active_particles == 0) { have_live_particles = false; }

			// Defragment particles if it's getting sparse.
			// (Don't bother if it's *empty*.)
			if((particles.size() > k_defragment_threshold) &&
				((active_particles * k_defragment_factor) < particles.size())) {
				defragment_particles();
			}

			// We *had* live particles, so we should draw the impact of them.
			needs_paint = true;
		}

		// Simulate the static particle mass.
		needs_paint |= static_particles.simulate(*this, dropout,
			[&](auto x, auto y){return digital_clock.solid_at(x, y);});
	}

	bool want_render() override { return needs_paint; }

	void render(SDL_Surface* fb) override {
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
		needs_paint = false;
	}

	Uint32 tick_duration() override { return 33; } // 30Hz
};

std::unique_ptr<Hack::Base> MakePopClock(int w, int h) {
	return std::make_unique<PopClock>(w, h);
}

}; // namespace Hack
