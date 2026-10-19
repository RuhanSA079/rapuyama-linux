#!/bin/sh
# the terminal for the 640x480 panel, maximized so it never runs off the
# screen. foot redraws only the cells that changed (weston-terminal redraws
# the whole window on every key = a full-screen push over the slow vc bus)
if [ -x /usr/bin/foot ]; then
	exec /usr/bin/foot --maximized "$@"
fi
exec /usr/bin/weston-terminal --maximized "$@"