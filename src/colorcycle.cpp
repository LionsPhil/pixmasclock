/* Display debugging, not actually a color cycle. */

#include <cassert>
#include <cmath>
#include <ctime>

#include <algorithm>
#include <functional>
#include <random>
#include <stdexcept>
#include <vector>

#include "hack.hpp"

namespace Hack {
struct ColorCycle : public Hack::Base {
	double hue;
	bool black;

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

	ColorCycle()
		: hue(0.0), black(false) {}

	void simulate() override {
		/*black = !black;
		if(black) {
			hue += 0.01;
			if(hue > 6.0) { hue = 0.0; }
		}*/
	}

	void render(SDL_Surface* fb) override {
		/*if(black) {
			SDL_FillRect(fb, nullptr,
				SDL_MapRGB(fb->format, 0, 0, 0));
		} else {
			Uint8 r, g, b;
			hue_to_rgb(hue, r, g, b);
			SDL_FillRect(fb, nullptr,
				SDL_MapRGB(fb->format, r, g, b));
		}*/
		static Uint8 r, g, b;
		/*if(r) {
			r = 0;
			g = 255;
		} else if(g) {
			g = 0;
			b = 255;
		} else if(b) {
			b = 0;
		} else {
			r = 255;
		}*/
		b = /*b ? 0 :*/ 255;
		SDL_FillRect(fb, nullptr,
			SDL_MapRGB(fb->format, r, g, b));
	}

	Uint32 tick_duration() override { return 33; } // 30Hz
};

std::unique_ptr<Hack::Base> MakeColorCycle() {
	return std::make_unique<ColorCycle>();
}

}; // namespace Hack
