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
	int q_x[4], q_y[4], q_w, q_h;
	bool q_down[4];
	std::string next_hack_;

	enum class Page { TOP, CHOOSE_HACK, SLEEP, SHUTDOWN };
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
		q_x[0] = w/kHSlices; q_y[0] = h/kVSlices;
		q_x[1] = q_w + ((w*2)/kHSlices); q_y[1] = q_y[0];
		q_x[2] = q_x[0]; q_y[2] = q_h + ((h*2)/kVSlices);
		q_x[3] = q_x[1]; q_y[3] = q_y[2];
		for(int i = 0; i < 4; ++i) { q_down[i] = false; }
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

		SDL_Color whiteish  = { 0x70, 0x70, 0x70, 0xff };
		SDL_Color reddish   = { 0xa0, 0x20, 0x20, 0xff };
		SDL_Color greenish  = { 0x20, 0xa0, 0x20, 0xff };
		SDL_Color yellowish = { 0xa0, 0x70, 0x20, 0xff };
		SDL_Color blueish   = { 0x20, 0x20, 0xc0, 0xff };

		switch(page) {
			case Page::TOP: {
				const char* labels[] =
					{"Resume", "Change\ndisplay", "Screen\noff", "Shut\ndown"};
				SDL_Color* colors[] =
					{&greenish, &blueish, &yellowish, &reddish};
				for(int i = 0; i < 4; ++i) {
					button(fb, labels[i], q_x[i], q_y[i], q_w, q_h,
						*colors[i], q_down[i]);
				}
				} break;
			case Page::CHOOSE_HACK: {
				const char* labels[] =
					{"Snow", "Pop"};
				SDL_Color* colors[] =
					{&whiteish, &reddish};
				for(int i = 0; i < 2; ++i) {
					button(fb, labels[i], q_x[i], q_y[i], q_w, q_h,
						*colors[i], q_down[i]);
				}
				} break;
			case Page::SLEEP:
				// This should end up being rendered *after* the backlight is
				// off, but at least will become visible if it turns back on for
				// some reason.
				text_at(fb, "Sleeping; tap to wake", 8, 8, whiteish, w-16);
				break;
			case Page::SHUTDOWN:
				// This should be visible until the init system kills us.
				text_at(fb, "Shutting down\n\nUnplug once screen blank",
					8, 8, whiteish, w-16);
				break;
		}
	}

	MenuResult click(int quarter) {
		switch(page) {
			case Page::TOP:
				switch(quarter) {
					case 0: return MenuResult::RETURN_TO_HACK; // Resume
					case 1: // Change display
						page = Page::CHOOSE_HACK;
						return MenuResult::KEEP_MENU;
					case 2: // Screen off
						page = Page::SLEEP;
						return MenuResult::SCREEN_OFF;
					case 3: // Shut down
						page = Page::SHUTDOWN;
						return MenuResult::SHUTDOWN;
				}
				break;
			case Page::CHOOSE_HACK:
				switch(quarter) {
					case 0: // Snow
						next_hack_="snowclock"; return MenuResult::CHANGE_HACK;
					case 1: // Pop
						next_hack_="popclock"; return MenuResult::CHANGE_HACK;
					default: return MenuResult::KEEP_MENU;
				}
				break;
			case Page::SLEEP:
				return MenuResult::WAKE; // Wake up and return to hack
				break;
			case Page::SHUTDOWN:
				return MenuResult::KEEP_MENU; // Mid-shutdown click ignored
				break;
		}
		assert(false);
		return MenuResult::RETURN_TO_HACK; // Should never happen™.
	}

	// 50Hz, but not really; we only get to act on events.
	Uint32 tick_duration() override { return 20; }

	MenuResult event(SDL_Event* event) override {
		switch(event->type) {
			case SDL_MOUSEBUTTONDOWN:
				{
					int x = event->button.x;
					int y = event->button.y;
					for(int i = 0; i < 4; ++i) {
						if((x >= q_x[i]) && (x <= q_x[i] + q_w)
						&& (y >= q_y[i]) && (y <= q_y[i] + q_h)) {
							q_down[i] = true;
						}
					}
				}
				break;
			case SDL_MOUSEMOTION:
				{
					int x = event->motion.x;
					int y = event->motion.y;
					for(int i = 0; i < 4; ++i) {
						if((x >= q_x[i]) && (x <= q_x[i] + q_w)
						&& (y >= q_y[i]) && (y <= q_y[i] + q_h)) {
							// Nothing; but clear if we moved *out* of it.
							// Does not implement re-entering the same button.
						} else {
							q_down[i] = false;
						}
					}
				}
				break;
			case SDL_MOUSEBUTTONUP: {
				MenuResult result = MenuResult::KEEP_MENU;
				for(int i = 0; i < 4; ++i) {
					if(q_down[i]) {
						result = click(i);
					}
					q_down[i] = false;
				}
				return result; }
			default:
				break;
		}
		return MenuResult::KEEP_MENU;
	}

	std::string next_hack() override { return next_hack_; }
};

std::unique_ptr<Hack::Base> MakeMenu(int w, int h, void* config) {
	return std::make_unique<Menu>(w, h, static_cast<cfg_t*>(config));
}

}; // namespace Hack
