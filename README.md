# Pixmas Clock

A digital clock for a Raspberry Pi with a framebuffer display, with festive snow that gathers on the display.

**Disclaimer:** This is pretty much made just for me as a weekend(s) project, and is thrown online in case it's handy (or cool) to someone else. Expect to have to hack it about a bit to make it work well for your case. (Likewise it's not the cleanest codebase in the world, ahem.)

**Note:** During less-festive times (like, the time of writing at this commit) I tend to reconfigure it to do something, well, less-festive. It's currently doing a cool thing with clock segments popping into rainbow sand as they change instead of snow. Fiddle with the factory functions in `main` to pick something else.

## Demo

[![Snow gathering on clock digits, that falls from them, and off the bottom of the screen, as the hour changes.](http://img.youtube.com/vi/hGhVkTMxfyE/0.jpg)](https://www.youtube.com/watch?v=hGhVkTMxfyE "Watch the hour change on YouTube")

Snow gathers on the digits, and falls off them when they change. It also builds up at the bottom of the screen, and falls away from that during the first fifteen seconds of each hour. The color of the time gently fades between red and green every other minute. Snow in the background flutters gently in response to simulated breezes (but there's no fluid dynamics for getting kicked up by snowfalls, etc.).

## Hardware

I targetted this for the Tontec mz61581-pi-ext on an original Raspberry Pi B+ I got as a gift (which managed to snare me into buying far too many Pis, apparently), which is a neat little GPIO-attached screen and transparent case...and is for an older header format that current Pis and has had its Amazon product page replaced with a different one.

(If you have the hardware, these links might help set it up: [1](https://theezitguy.wordpress.com/2016/01/17/raspberry-pi-tontec-3-5-screen-installation/), [2](https://www.raspberrypi.org/forums/viewtopic.php?f=91&t=100311))

Still, SDL means this should target anything. It's *deliberately* SDL 1.2 code so it can support raw framebuffers, so you don't *have* to run X11 (although it'll work with that too, and opens in a window on x64_64 for development). It's also fair simplistic in its SDL usage, so migrating to 2 should be easy if you want to---as should be adapting it to other embedded displays.

## Building

You want Debian/Rapsbian packages `clang libsdl1.2-dev`. Either `make` or open in Visual Studio Code.

## Running

`make run` will build (if necessary) and run the binary.

Note there's a really sloppy check in the Makefile that sets a `DESKTOP` compiler define that instead launches in windowed mode. If you're doing development on a laptop/desktop that's not `x86_64`, you'll need to change that.

### ...on the Pi

For the Tontec GPIO display `make runonpi` will set the right environment variables to target the framebuffer, and run via `sudo` to get permissions for it.

You can trade off having to run as root for opening permissions on `/dev/console`. Unfortunately on Raspbian it's `root:root`, so there's no simple group to join.
