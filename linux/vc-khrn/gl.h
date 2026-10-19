// SPDX-License-Identifier: GPL-2.0-only
/* what egl.c and gles2.c share */
#ifndef GL_H
#define GL_H

/* a new context is current: the client side GL state starts over */
void glgl_reset(void);

/*
 * the native window of this EGL: where the window goes on the 640x480
 * screen, NULL = all of it
 */
struct vc_window{
	int x, y, width, height;
};

#endif