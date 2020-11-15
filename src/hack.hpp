#ifndef HACK_HPP_
#define HACK_HPP_

#include <SDL.h>

// Graphical hack interface

namespace Hack {
struct Base {
	virtual ~Base() {}
	virtual void simulate() = 0;
	virtual void render() = 0;
	virtual Uint32 tick_duration() = 0;
};
};

#endif
