#ifndef HACK_HPP_
#define HACK_HPP_

/* Graphical hack interface.
 * "Hack" here being used in the same sense as xscreensaver: some neat code to
 * do a pretty thing.
 */

#include <memory>

#include <SDL.h>

namespace Hack {
	struct Base {
		virtual ~Base() {}
		virtual void simulate() = 0;
		virtual void render() = 0;
		virtual Uint32 tick_duration() = 0;
	};

	/* Just dumping some factory functions here. You could make this all
	 * self-registering factory, but that's the boring bit and my weekend
	 * project is to make pixels move all pretty, not do more infra code again
	 * for the 100th time.They're not even runtime switchable at the
	 * moment anyway. */

	std::unique_ptr<Hack::Base> MakeSnowFP(SDL_Surface* framebuffer);
	std::unique_ptr<Hack::Base> MakeSnowInt(SDL_Surface* framebuffer);
	std::unique_ptr<Hack::Base> MakeSnowClock(SDL_Surface* framebuffer);
	std::unique_ptr<Hack::Base> MakePopClock(SDL_Surface* framebuffer);
};

#endif
