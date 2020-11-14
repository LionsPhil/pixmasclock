## Running on the Pi

For the Tontec GPIO display:

`sudo SDL_VIDEODRIVER="fbcon" SDL_FBDEV="/dev/fb1" ./pixmas`

You can trade off having to run as root for opening permissions on `/dev/console`. Unfortunately on Raspbian it's `root:root`, so there's no simple group to join.
