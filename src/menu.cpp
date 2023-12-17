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
#include <SDL2/SDL_ttf.h> // libsdl2-ttf-dev

#include "hack.hpp"

const char* kFontFile = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";

namespace Hack {
struct Menu : public Hack::Base {
	int w, h;
	cfg_t* config;
	TTF_Font* font;
	int q1_x, q1_y, q2_x, q2_y, q3_x, q3_y, q4_x, q4_y, q_w, q_h;
	// bool q1_down, q2_down, q3_down, q4_down;

	enum class Page { TOP };
	Page page;

	Menu(int w, int h, cfg_t* config)
		: w(w), h(h), config(config), page(Page::TOP) {

		if(TTF_Init() != 0) { throw std::runtime_error(TTF_GetError()); }
		const int kScaler = 10; // Smaller = larger fraction of the screen.
		font = TTF_OpenFont(kFontFile, ((w*3)/(4*kScaler))); // 3/4 approx pt/px
		if(font == nullptr) { throw std::runtime_error(TTF_GetError()); }

		// Co-ordinates for four-quarter buttons, with spacing based on slices.
		const int kHSlices = 21;
		const int kVSlices = 15;
		q_w = (w*((kHSlices-3)/2))/kHSlices;
		q_h = (h*((kVSlices-3)/2))/kVSlices;
		q1_x = w/kHSlices; q1_y = h/kVSlices;
		q2_x = q_w + ((w*2)/kHSlices); q2_y = q1_y;
		q3_x = q1_x; q3_y = q_h + ((h*2)/kVSlices);
		q4_x = q2_x; q4_y = q3_y;
	}

	~Menu() {
		TTF_CloseFont(font); // Explicitly safe even if nullptr.
		TTF_Quit();
	}

	void simulate() override {}

	void text_at(SDL_Surface* to, const char* text, int x, int y,
		SDL_Color color, int wrap = 0) {

		SDL_Surface* textsurf = TTF_RenderUTF8_Blended_Wrapped(
			font, text, color, wrap);
		if(textsurf == nullptr) { throw std::runtime_error(TTF_GetError()); }
		SDL_Rect dstrect = { x, y, textsurf->w, textsurf->h };
		if(SDL_BlitSurface(textsurf, nullptr, to, &dstrect) != 0)
			{ throw std::runtime_error(SDL_GetError()); }
		SDL_FreeSurface(textsurf);
	}

	Uint8 color_channel_brighten(Uint8 value, Sint16 add) {
		Sint16 result = value + add;
		if(result < 0x00) { return 0x00; }
		if(result > 0xff) { return 0xff; }
		return result;
	}

	SDL_Color color_brighten(const SDL_Color& color, Sint16 add) {
		return SDL_Color {
			color_channel_brighten(color.r, add),
			color_channel_brighten(color.g, add),
			color_channel_brighten(color.b, add),
			color_channel_brighten(color.a, add)
		};
	}

	void button(SDL_Surface* to, const char* label, int x, int y, int w, int h,
		SDL_Color color, bool down) {

		SDL_Color top_rim, body, bot_rim;
		if(down) {
			top_rim = color;
			body = color_brighten(color, 0x40);
			bot_rim = color_brighten(color, 0x80);
		} else {
			top_rim = color_brighten(color, 0x40);
			body = color;
			bot_rim = color_brighten(color, -0x40);
		}
		// Oh FillRect, why are you like this with colors.
		Uint32 u_top_rim = SDL_MapRGB(to->format, top_rim.r, top_rim.g, top_rim.b);
		Uint32 u_body = SDL_MapRGB(to->format, body.r, body.g, body.b);
		Uint32 u_bot_rim = SDL_MapRGB(to->format, bot_rim.r, bot_rim.g, bot_rim.b);

		// Yeah I'm not gonna make this hugely scalable, sorry.
		SDL_Rect rect;
		rect.x = x; rect.y = y; rect.w = w; rect.h = h;
		SDL_FillRect(to, &rect, u_body);
		rect.x = x; rect.y = y+h-4; rect.w = w; rect.h = 4;
		SDL_FillRect(to, &rect, u_bot_rim);
		rect.x = x+w-4; rect.y = y; rect.w = 4; rect.h = h;
		SDL_FillRect(to, &rect, u_bot_rim);
		rect.x = x; rect.y = y; rect.w = w; rect.h = 4;
		SDL_FillRect(to, &rect, u_top_rim);
		rect.x = x; rect.y = y; rect.w = 4; rect.h = h;
		SDL_FillRect(to, &rect, u_top_rim);
		SDL_Color white = { 0xff, 0xff, 0xff, 0xff };
		text_at(to, label, x + 8, y + 8, white, w - 16);
	}

	void render(SDL_Surface* fb) override {
		SDL_FillRect(fb, nullptr, SDL_MapRGB(fb->format, 0x00, 0x00, 0x00));

		//SDL_Color white = { 0xff, 0xff, 0xff, 0xff };
		SDL_Color reddish   = { 0xa0, 0x20, 0x20, 0xff };
		SDL_Color greenish  = { 0x20, 0xa0, 0x20, 0xff };
		SDL_Color yellowish = { 0xa0, 0x70, 0x20, 0xff };
		SDL_Color blueish   = { 0x20, 0x20, 0xc0, 0xff };

		switch(page) {
			case Page::TOP:
				button(fb, "Display", q1_x, q1_y, q_w, q_h, greenish, true);
				button(fb, "Sleep\ntime", q2_x, q2_y, q_w, q_h, blueish, false);
				button(fb, "Sleep\nnow", q3_x, q3_y, q_w, q_h, yellowish, false);
				button(fb, "Shut\ndown", q4_x, q4_y, q_w, q_h, reddish, false);
				break;
		}
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
