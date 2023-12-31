#include <cassert>
#include <stdexcept>

#include "digitalclock.hpp"

constexpr int k_hue_rotation_minutes = 30;


DigitalClock::Digit::Digit() {
	for(int s = 0; s < 7; ++s) {
		segment[s] = false;
		segrect[s] = { 0, 0, 0, 0 };
	}
}

void DigitalClock::Digit::size_for(Sint16 x, Sint16 y,
	Uint16 sw, Uint16 sh, Uint16 st) {

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

void DigitalClock::Digit::number(int n) {
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

void DigitalClock::Digit::render(SDL_Surface* fb) {
	for(int s = 0; s < 7; ++s) {
		if(segment[s]) { SDL_FillRect(fb, &segrect[s], 1); }
	}
}

DigitalClock::DigitalClock(int w, int h, bool hue_cycle) :
	hue_cycle_(hue_cycle), last_minute_(-1), last_second_(-1),
	fb(nullptr, SDL_FreeSurface) {
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
SDL_Surface* DigitalClock::make_surface(int w, int h) {
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> s(
		SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_ASYNCBLIT,
			w, h, 8, 0, 0, 0, 0),
		SDL_FreeSurface);
	if(s.get() == nullptr) { throw std::runtime_error(SDL_GetError()); }
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
#if SDLVERSION == 1
	if(SDL_SetColorKey(s.get(), SDL_SRCCOLORKEY | SDL_RLEACCEL, 0) != 0) {
		throw std::runtime_error("failed to set color key");
	}
#else
	if(SDL_SetSurfaceRLE(s.get(), SDL_TRUE) != 0) {
		throw std::runtime_error("failed to set RLE");
	}
	if(SDL_SetColorKey(s.get(), SDL_TRUE, 0) != 0) {
		throw std::runtime_error("failed to set color key");
	}
#endif
	return s.release();
}

// Do a big dirty sigmoid function hack to make hues more red.
// Hand-tuned constants to get *approximately* [0,1]->[0,1] ranges,
// although strictly sigmoid is [-inf,inf]->[0,1].
// It's too aggressive, though.
double DigitalClock::big_dirty_sigmoid(double x) {
	// In Wolfram Alpha-ese:
	// y=Divide[1,1+Power[4,-8\(40)x-0.5\(41)]]
	return 1.0 / (1.0 + pow(4.0, -8.0 * (x - 0.5)));
}

// This is better but ultimately I preferred leaving the hue alone.
double DigitalClock::big_dirty_sin(double x) {
	return 0.5 + 0.5 * (sin(M_PI * (x - 0.5)));
}

void DigitalClock::hue_to_rgb(double h,
	Uint8& out_r, Uint8& out_g, Uint8& out_b) {

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
bool DigitalClock::set_time(const std::tm* tm) {
	// This is an optimization to avoid recalculating the same time each
	// tick, which assumes we'll never jump to the same second in some
	// other time, which should be reasonable for a clock.
	if(last_second_ == tm->tm_sec) { return false; }
	last_second_ = tm->tm_sec;

	// Change the rainbow or festive hue based on the second.
	Uint8 r, g, b; Uint32 s;
	s = std::min(tm->tm_sec, 59); // no doing evil with leap seconds
	if (hue_cycle_) {
		s += 60 * (tm->tm_min % k_hue_rotation_minutes);
		hue_to_rgb(s/(60.0 * k_hue_rotation_minutes), r, g, b);
	} else {
		b = 0;
		if(tm->tm_min % 2) { s = 59 - s; }
		if(s < 30) {
			r = 255;
			g = (s*255)/29;
		} else {
			r = ((59-s)*255)/29;
			g = 255;
		}
	}
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

SDL_Surface* DigitalClock::rendered() { return fb.get(); }

bool DigitalClock::solid_at(int x, int y) {
	auto buffer = fb.get();
	assert(x >= 0); assert(x < buffer->w);
	assert(y >= 0); assert(y < buffer->h);
	return static_cast<Uint8 *>(buffer->pixels)
		[(x*bytes_per_pixel_)+(y*pitch_)] != 0;
}

DigitalClock::Digit& DigitalClock::get_digit(int i) {
	assert(i >= 0); assert (i <= 4);
	return digits[i];
}
