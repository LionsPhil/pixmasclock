/* Display debugging, not actually a color cycle. */

#include <cassert>
#include <cmath>
#include <ctime>

#include <algorithm>
#include <functional>
#include <random>
#include <stdexcept>
#include <vector>

#include <confuse.h>

#include "hack.hpp"

namespace Hack {
struct Menu : public Hack::Base {
	int w, h;
	cfg_t* config;

	Menu(int w, int h, cfg_t* config)
		: w(w), h(h), config(config) {}

	void simulate() override {}

	void render(SDL_Surface* fb) override {
		SDL_FillRect(fb, nullptr, SDL_MapRGB(fb->format, 0x00, 0x00, 0x00));
	}

	// 50Hz, but not really; we only get to act on events.
	Uint32 tick_duration() override { return 20; }

	bool event(SDL_Event* event) override {
		switch(event->type) {
			case SDL_MOUSEBUTTONUP:
				return false;
			default:
				break;
		}
		return true;
	}
};

std::unique_ptr<Hack::Base> MakeMenu(int w, int h, void* config) {
	return std::make_unique<Menu>(w, h, static_cast<cfg_t*>(config));
}

}; // namespace Hack
