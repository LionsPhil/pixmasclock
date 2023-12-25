// Copyright (c) 2023 Philip Boulain; see LICENSE for terms.
#include <exception>
#include <iostream>
#include <memory>

// The cryptic Reason for menu only with SDL 2 is somewhat that I don't want to
// pull this lib into the very embedded Tontec framebuffer version.
#include <confuse.h>

// Force VSCode to know this is going to get the version 2 define here.
#define SDLVERSION 2
#include "hack.hpp"

const char* kConfigFile = "~/.config/pixmas.conf";

#ifdef DESKTOP
const char* kCommandBacklightOn = "echo fake backlight on 2>&1";
const char* kCommandBacklightOff = "echo fake backlight off 2>&1";
const char* kCommandShutdown = "echo fake shutdown 2>&1";
#else
const char* kCommandBacklightOn = "echo 1 | sudo tee /sys/class/backlight/backlight/brightness";
const char* kCommandBacklightOff = "echo 0 | sudo tee /sys/class/backlight/backlight/brightness";
const char* kCommandShutdown = "sudo poweroff";
#endif

// This is just used to get SDL init/deinit via RAII for nice error handling.
namespace SDL {
	struct Error : public std::exception {
		virtual const char* what() const throw()
			{ return SDL_GetError(); }
	};

	struct Graphics {
		SDL_Window* window;
		SDL_Renderer *renderer;
		SDL_Texture* texture;
		int w, h;

		Graphics(int virtual_w, int virtual_h) {
			// Requesting 0 x 0 will use native resolution for virtual, but...
			if(virtual_w == 0) { virtual_h = 0; }
			if(virtual_h == 0) { virtual_w = 0; } // no mismatching
#ifdef DESKTOP
			if(virtual_w == 0) {
				// Resolution of the Pimoroni HyperPixel, rather than true.
				virtual_w = 800; virtual_h = 480;
			}
#endif
			if(SDL_Init(SDL_INIT_VIDEO) != 0) { throw Error(); }
			if(SDL_CreateWindowAndRenderer(
				virtual_w, virtual_h,
#ifdef DESKTOP
				0,
#else
				// Not a "desktop" computer: use fullscreen.
				SDL_WINDOW_FULLSCREEN_DESKTOP,
#endif
				&window, &renderer) != 0 ) { throw Error(); }
			SDL_SetWindowTitle(window, "pixmas");
#ifndef DESKTOP
			SDL_SetWindowAlwaysOnTop(window, SDL_TRUE);
#endif
			if(SDL_GetRendererOutputSize(renderer, &w, &h) != 0)
				{ throw Error(); }
			texture = SDL_CreateTexture(renderer,
							SDL_PIXELFORMAT_ARGB8888,
							SDL_TEXTUREACCESS_STREAMING,
							w, h);
		}
		~Graphics() {
			// Let SDL free all its own things.
			SDL_Quit();
		}
	};
};

std::unique_ptr<Hack::Base> change_hack(SDL::Graphics& graphics,
	std::string hackname) {

	std::unique_ptr<Hack::Base> hack;
	SDL_Surface* tmp_fb = nullptr;
	if(SDL_LockTextureToSurface(graphics.texture, NULL, &tmp_fb) != 0)
		{ throw std::runtime_error(SDL_GetError()); }

	// (Still can't be bothered to set up a self-registering factory.)
	if(hackname == "snowfp") {
		hack = Hack::MakeSnowFP(graphics.w, graphics.h, tmp_fb->format);
	} else if(hackname == "snowint") {
		hack = Hack::MakeSnowInt(graphics.w, graphics.h, tmp_fb->format);
	} else if(hackname == "snowclock") {
		hack = Hack::MakeSnowClock(graphics.w, graphics.h);
	} else if(hackname == "popclock") {
		hack = Hack::MakePopClock(graphics.w, graphics.h);
	} else if(hackname == "colorcycle") {
		hack = Hack::MakeColorCycle();
	} else {
		std::cerr << "Unknown hack '" << hackname << "'" << std::endl;
		hack = Hack::MakeColorCycle();
	}

	SDL_UnlockTexture(graphics.texture);
	return hack;
}

void render_hack(SDL::Graphics& graphics, Hack::Base* hack) {
	if(hack->want_render()) {
		SDL_Surface* fb = nullptr;
		if(SDL_LockTextureToSurface(graphics.texture, NULL, &fb) != 0)
			{ throw std::runtime_error(SDL_GetError()); }
		hack->render(fb);
		SDL_UnlockTexture(graphics.texture); // Also frees fb
		fb = nullptr;
		SDL_RenderClear(graphics.renderer);
		SDL_RenderCopy(graphics.renderer, graphics.texture,
			NULL, NULL);
		SDL_RenderPresent(graphics.renderer);
	}
}

void try_system(const char* cmd) {
	int rval = system(cmd);
	if(rval == -1) {
		int e = errno;
		std::cerr << "Could not create child: " << strerror(e) << std::endl;
	} else {
		if(WIFEXITED(rval)) {
			int status = WEXITSTATUS(rval);
			if(status != 0) {
				std::cerr << "'" << cmd << "' exited status " << status
					<< std::endl;
			}
		} else {
			if(WIFSIGNALED(rval)) {
				std::cerr << "'" << cmd << "' killed by signal "
					<< WTERMSIG(rval) << std::endl;
			} else {
				std::cerr << "'" << cmd << "' exited mysteriously" << std::endl;
			}
		}
	}
}

void backlight(bool on) {
	std::cerr << "Attempting to turn backlight " << (on ? "on" : "off")
		<< std::endl;
	try_system(on ? kCommandBacklightOn : kCommandBacklightOff);
}

void shutdown() {
	std::cerr << "Attempting shutdown" << std::endl;
	try_system(kCommandShutdown);
}

void save_config(cfg_t* config) {
	// Look out, it's C.
	char* expanded = cfg_tilde_expand(kConfigFile);
	FILE *fp = fopen(expanded, "w");
	free(expanded);
	cfg_print(config, fp);
	fclose(fp);
}

// Different event loop logic and nesting to preserve underlying hack.
void menu(SDL::Graphics& graphics, cfg_t* config,
	std::unique_ptr<Hack::Base>& hack) {

	std::unique_ptr<Hack::Base> menu_hack =
		Hack::MakeMenu(graphics.w, graphics.h, config);
	render_hack(graphics, menu_hack.get());
	SDL_Event event;
	bool run = true;
	// Process events; blocking, unlike below, and interruptable by the run flag
	// inbetween each individual event.
	while(run && SDL_WaitEvent(&event)) {
		// Proc event.
		Hack::MenuResult result = menu_hack->event(&event);
		switch(result) {
			case Hack::MenuResult::CHANGE_HACK:
				hack = change_hack(graphics, menu_hack->next_hack());
				cfg_setstr(config, "hack", menu_hack->next_hack().c_str());
				save_config(config);
				// fall through
			case Hack::MenuResult::RETURN_TO_HACK:
				run = false; break;
			case Hack::MenuResult::SCREEN_OFF:
				// Stay in the menu and wait for the tap to wake again.
				backlight(false);
				break;
			case Hack::MenuResult::WAKE:
				backlight(true);
				run = false;
				break;
			case Hack::MenuResult::QUIT:
				run = false;
				{
					// Create a quit event to push into the main loop.
					SDL_Event quit_event;
					quit_event.type = SDL_QUIT;
					quit_event.quit.timestamp = SDL_GetTicks();
					SDL_PushEvent(&quit_event);
				}
				break;
			case Hack::MenuResult::SHUTDOWN:
				// Stay in the menu and wait for the QUIT event.
				shutdown();
				break;
			default: break; // KEEP_MENU
		}
		switch(event.type) {
			case SDL_QUIT:
				run = false;
				// Reprocess this in the main() event loop to quit entirely.
				SDL_PushEvent(&event);
				break;
			default:
				break;
		}
		// Sim & render menu.
		menu_hack->simulate();
		render_hack(graphics, menu_hack.get());
	}
}

int main(int argc, char** argv) {
	cfg_opt_t config_options[] =
	{
		CFG_STR("hack", "snowclock", CFGF_NONE),
		CFG_INT("w", 0, CFGF_NONE),
		CFG_INT("h", 0, CFGF_NONE),
		CFG_END()
	};
	cfg_t* config = cfg_init(config_options, CFGF_NONE);
	// This apparently will print on failure all by itself.
	cfg_parse(config, kConfigFile);

	SDL::Graphics graphics(cfg_getint(config, "w"), cfg_getint(config, "h"));
#ifndef DESKTOP
	SDL_ShowCursor(0);
#endif
	SDL_SetRenderDrawColor(graphics.renderer, 0x77, 0x77, 0x77, 0xff);
	SDL_RenderClear(graphics.renderer);
	SDL_RenderPresent(graphics.renderer);

	std::unique_ptr<Hack::Base> hack =
		change_hack(graphics, cfg_getstr(config, "hack"));

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
				break;
			case SDL_MOUSEBUTTONUP:
				// Go to the menu.
				menu(graphics, config, hack);
				// Skip sim time forward so we don't try to catch up.
				ticklast = SDL_GetTicks();
				break;
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
			if(tickerror > hack->tick_duration() * 10) {
				static bool once = false;
				if(!once) {
					std::cerr << "Running too slow! Skipping ticks!" << std::endl;
					once = true;
				}
				tickerror = hack->tick_duration();
			}
			do {
				tickerror -= hack->tick_duration();
				hack->simulate();
			} while(tickerror >= hack->tick_duration());

			render_hack(graphics, hack.get());
		} else {
			/// Have a nap until we actually have at least one tick to run.
			SDL_Delay(hack->tick_duration());
		}
	}

	cfg_free(config);
	return EXIT_SUCCESS;
}
