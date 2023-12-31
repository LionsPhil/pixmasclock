#ifndef DIGITALCLOCK_HPP_
#define DIGITALCLOCK_HPP_

#include <ctime>

#include "hack.hpp"

class DigitalClock {
	struct Digit {
		bool segment[7];
		SDL_Rect segrect[7];

		Digit();
		// sw and sh are *segment* height and width; st segment thickness.
		// Total render dimensions will be (sw, sh+st) due to the midline.
		void size_for(Sint16 x, Sint16 y, Uint16 sw, Uint16 sh, Uint16 st);
		void number(int n);
		void render(SDL_Surface* fb);
	};
	Digit digits[4];

	bool hue_cycle_;
	int last_minute_;
	int last_second_;
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> fb;
	// cache format info
	Uint8 bytes_per_pixel_;
	Uint16 pitch_;

public:
	DigitalClock(int w, int h, bool hue_cycle);

	// Make a framebuffer for the clock graphics, which can also be read
	// back for its physics (but that was a bad idea). Only uses two colors.
	// The clock does this automatically for its own internal surface.
	SDL_Surface* make_surface(int w, int h);
	// Do a big dirty sigmoid function hack to make hues more red.
	// Hand-tuned constants to get *approximately* [0,1]->[0,1] ranges,
	// although strictly sigmoid is [-inf,inf]->[0,1].
	// It's too aggressive, though.
	double big_dirty_sigmoid(double x);
	// This is better but ultimately I preferred leaving the hue alone.
	double big_dirty_sin(double x);
	void hue_to_rgb(double h, Uint8& out_r, Uint8& out_g, Uint8& out_b);
	// Returns true if solid regions have changed.
	bool set_time(const std::tm* tm);
	SDL_Surface* rendered(); // treat as const
	bool solid_at(int x, int y);
	Digit& get_digit(int i); // treat as const
};

#endif
