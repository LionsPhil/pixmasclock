#ifndef HACK_HPP_
#define HACK_HPP_

/* Graphical hack interface.
 * "Hack" here being used in the same sense as xscreensaver: some neat code to
 * do a pretty thing.
 */

#include <memory>

// This is a bit wrong, given sdl-config output, but, makes VSCode happy? :/
#ifndef SDLVERSION
	#error SDLVERSION not set, but the Makefile should have?
#endif
#if SDLVERSION == 1
	#include <SDL/SDL.h>
#else
	#include <SDL2/SDL.h>
	// Dirty compat hacks; this flag is gone.
	#define SDL_ASYNCBLIT 0
	// Dirty compat hacks; this is a slightly gratuitous API change.
	// https://stackoverflow.com/questions/29609544/how-to-use-palettes-in-sdl-2
	inline int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors,
		int firstcolor, int ncolors) {
		// Note change from true-success/false-fail to 0-success/negative-fail.
		return SDL_SetPaletteColors(surface->format->palette, colors,
			firstcolor, ncolors) == 0;
	}
#endif

namespace Hack {
	enum class MenuResult
		{ KEEP_MENU, RETURN_TO_HACK, CHANGE_HACK, SCREEN_OFF, WAKE, SHUTDOWN };

	struct Base {
		virtual ~Base() {}
		virtual void simulate() = 0;
		// Return true if render() should be called, else is skipped.
		inline virtual bool want_render() { return true; }
		virtual void render(SDL_Surface* fb) = 0;
		virtual Uint32 tick_duration() = 0;

		// For the menu only, process an event.
		inline virtual MenuResult event(SDL_Event* event)
			{ return MenuResult::RETURN_TO_HACK; }
		inline virtual std::string next_hack() { return ""; } // also menu only
	};

	/* Just dumping some factory functions here. You could make this all
	 * self-registering factory, but that's the boring bit and my weekend
	 * project is to make pixels move all pretty, not do more infra code again
	 * for the 100th time.They're not even runtime switchable at the
	 * moment anyway. */

	// Do NOT hold onto the PixelFormat; it is only valid during the c'tor,
	// mostly for awkward legacy reasons.

#if SDLVERSION != 1
	std::unique_ptr<Hack::Base> MakeMenu(int w, int h, void* config);
#endif
	std::unique_ptr<Hack::Base> MakeSnowFP(int w, int h, SDL_PixelFormat* fmt);
	std::unique_ptr<Hack::Base> MakeSnowInt(int w, int h, SDL_PixelFormat* fmt);
	std::unique_ptr<Hack::Base> MakeSnowClock(int w, int h);
	std::unique_ptr<Hack::Base> MakePopClock(int w, int h);
	std::unique_ptr<Hack::Base> MakeColorCycle();
};

#endif
