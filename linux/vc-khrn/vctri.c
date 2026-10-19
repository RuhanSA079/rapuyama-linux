// SPDX-License-Identifier: GPL-2.0-only
/* a turning triangle (tex: a chequered square) drawn by the VC. usage: vctri [seconds] [vbo|tex] */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *vs=
	"attribute vec2 pos;\n"
	"attribute vec3 col;\n"
	"uniform float angle;\n"
	"varying vec3 c;\n"
	"void main(){\n"
	"	float s=sin(angle), k=cos(angle);\n"
	"	gl_Position=vec4(pos.x*k-pos.y*s, pos.x*s+pos.y*k, 0.0, 1.0);\n"
	"	c=col;\n"
	"}\n";
static const char *fs=
	"precision mediump float;\n"
	"varying vec3 c;\n"
	"void main(){\n"
	"	gl_FragColor=vec4(c, 1.0);\n"
	"}\n";

static const char *tex_fs=
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"varying vec3 c;\n"
	"void main(){\n"
	"	gl_FragColor=texture2D(tex, c.xy);\n"
	"}\n";

/* x, y, u, v, 0: the colour attrib carries the texture coords */
static const GLfloat quad[]={
	-0.6f, -0.6f, 0, 0, 0,
	0.6f, -0.6f, 1, 0, 0,
	-0.6f, 0.6f, 0, 1, 0,
	0.6f, 0.6f, 1, 1, 0,
};

static const GLfloat tri[]={
	0.0f, 0.8f, 1, 0, 0,
	-0.7f, -0.6f, 0, 1, 0,
	0.7f, -0.6f, 0, 0, 1,
};

static GLuint shader(GLenum type, const char *src){
	GLuint s=glCreateShader(type);
	GLint ok=0;

	glShaderSource(s, 1, &src, 0);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	printf("%s shader %u: compile %s\n", type==GL_VERTEX_SHADER ? "vertex" : "fragment", s, ok ? "ok" : "FAILED");
	return s;
}

static double now(void){
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec+t.tv_nsec/1e9;
}

int main(int argc, char **argv){
	double secs=argc>1 ? atof(argv[1]) : 10, t0, t;
	int vbo=argc>2 && !strcmp(argv[2], "vbo"), tex=argc>2 && !strcmp(argv[2], "tex"), frames=0;
	const GLfloat *verts=tex ? quad : tri;
	EGLint n=0, ok=0;
	EGLConfig cfg;
	EGLDisplay dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint ctx_attr[]={EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	GLuint prog, buf=0;
	GLint angle;

	if(!eglInitialize(dpy, 0, 0)){fprintf(stderr, "eglInitialize failed: %#x\n", eglGetError()); return 1;}
	eglChooseConfig(dpy, 0, &cfg, 1, &n);
	EGLContext ctx=eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	EGLSurface surf=eglCreateWindowSurface(dpy, cfg, 0, 0);
	if(ctx==EGL_NO_CONTEXT || surf==EGL_NO_SURFACE){fprintf(stderr, "no context / surface: %#x\n", eglGetError()); return 1;}
	eglMakeCurrent(dpy, surf, surf, ctx);
	printf("%s, %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	prog=glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, tex ? tex_fs : fs));
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "col");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	printf("program %u: link %s\n", prog, ok ? "ok" : "FAILED");
	glUseProgram(prog);
	angle=glGetUniformLocation(prog, "angle");
	printf("angle uniform at %d\n", angle);

	if(vbo){
		glGenBuffers(1, &buf);
		glBindBuffer(GL_ARRAY_BUFFER, buf);
		glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
	}
	if(tex){
		/* 64x64 rgb565 chequers, 8x8 squares */
		static unsigned short px[64*64];
		GLuint t;

		for(int i=0; i<64*64; i++){px[i]=(((i%64)/8+(i/64/8))&1) ? 0xffe0 : 0x001f;}
		glGenTextures(1, &t);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, t);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, px);
		glUniform1i(glGetUniformLocation(prog, "tex"), 0);
	}
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, vbo ? (void *)0 : verts);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, vbo ? (void *)8 : verts+2);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glViewport(0, 0, 640, 480);

	t0=now();
	do{
		t=now()-t0;
		glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(angle, (float)t);
		glDrawArrays(tex ? GL_TRIANGLE_STRIP : GL_TRIANGLES, 0, tex ? 4 : 3);
		eglSwapBuffers(dpy, surf);
		frames++;
	}while(t<secs);
	printf("%d frames in %.1f s = %.1f fps\n", frames, t, frames/t);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surf);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	return 0;
}