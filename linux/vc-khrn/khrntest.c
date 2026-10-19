// SPDX-License-Identifier: GPL-2.0-only
/* the VC clears a window in the middle of the screen. usage: khrntest [rrggbb] [seconds] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "khrn.h"

#define W 320
#define H 240
#define KEY 0xe6000002		/* window key, see videocore.c vc_khrn_draw */
#define GCE_SURFACE 0x96000002	/* one-handle GCE surface, id 2 */

static uint32_t fbits(float f){
	uint32_t u;

	memcpy(&u, &f, 4);
	return u;
}

/* "res" replies carry their length in front */
static uint32_t word(const uint32_t *r, int len, int i){
	if(len>=4 && r[0]==(uint32_t)len-4){i++;}
	return len>=4*(i+1) ? r[i] : 0;
}

int main(int argc, char **argv){
	unsigned long rgb=argc>1 ? strtoul(argv[1], 0, 16) : 0xff00ff;
	int secs=argc>2 ? atoi(argv[2]) : 10;
	uint32_t ctx, surf, reg, update, el, r[8], a[17];
	int ret, len;

	ret=khrn_init();
	if(ret){fprintf(stderr, "no /dev/vc-khrn: %s\n", strerror(-ret)); return 1;}

	khrn_current(0, 0, 0, 0);
	{
		uint32_t c[3]={KHRN_EGL_CREATE_GLES20, 0, 1};

		ret=khrn_call_int(c, 3, &ctx);
		printf("context -> %d, %#x\n", ret, ctx);
		if(ret || !ctx){return 1;}
	}
	{
		uint32_t c[13]={KHRN_EGL_CREATE_SURFACE, KEY, 1, W, H, KHRN_RGB_565,
				KHRN_FMT_NONE, KHRN_FMT_NONE, 0, 0, 0, 0, 0};

		len=khrn_call(c, 13, 0, 0, 0, r, sizeof(r));
		surf=word(r, len, 2);
		printf("window surface -> %d, %#x\n", len, surf);
		if(!surf){return 1;}
	}
	a[0]=GCE_SURFACE;
	a[1]=KEY;
	a[2]=VC_IMG_TF_RGB565;
	a[3]=W;
	a[4]=H;
	ret=disp_call(DISP_GCE_REGISTER, a, 5, 1, &reg);
	printf("register -> %d, %#x (0x28000 = 565 tiled)\n", ret, reg);
	if(ret || (int32_t)reg<0){return 1;}

	khrn_current(KHRN_GL20, ctx, surf, surf);
	{
		uint32_t c[10]={KHRN_GL_CLEAR_COLOR, fbits((rgb>>16&0xff)/255.0f),
				fbits((rgb>>8&0xff)/255.0f), fbits((rgb&0xff)/255.0f), fbits(1.0f),
				KHRN_GL_CLEAR, 0x4000, KHRN_EGL_FLUSH_WAIT, 1, 0};
		uint32_t done;

		ret=khrn_call_int(c, 10, &done);
		printf("clear + flush -> %d\n", ret);
		if(ret){return 1;}
	}
	{
		uint32_t c[6]={KHRN_EGL_SWAP, surf, W, H, KEY, 0};

		ret=khrn_call(c, 6, 0, 0, 0, 0, 0);
		printf("swap -> %d\n", ret);
	}
	usleep(500000);

	a[0]=1;
	if(disp_call(DISP_UPDATE_START, a, 1, 1, &update)){return 1;}
	a[0]=update;
	a[1]=GCE_SURFACE;
	a[2]=0;
	a[3]=KEY;
	a[4]=khrn_display();
	a[5]=10;
	a[6]=(640-W)/2;
	a[7]=(480-H)/2;
	a[8]=W;
	a[9]=H;
	a[10]=0;
	a[11]=0;
	a[12]=W<<16;
	a[13]=H<<16;
	a[14]=0;
	a[15]=0xff;
	a[16]=0;
	ret=disp_call(DISP_GCE_ELEMENT_ADD, a, 17, 1, &el);
	printf("GCE element -> %d, %#x\n", ret, el);
	a[0]=update;
	disp_call(DISP_SUBMIT_SYNC, a, 1, 1, 0);
	sleep(secs);

	if(!ret && el && el!=0xffffffff){
		a[0]=1;
		if(!disp_call(DISP_UPDATE_START, a, 1, 1, &update)){
			a[0]=update;
			a[1]=el;
			disp_call(DISP_ELEMENT_REMOVE, a, 2, 0, 0);
			a[0]=update;
			disp_call(DISP_SUBMIT_SYNC, a, 1, 1, 0);
		}
	}
	khrn_current(0, 0, 0, 0);
	{
		uint32_t c[2]={KHRN_EGL_DESTROY_SURFACE, surf};

		khrn_call(c, 2, 0, 0, 0, 0, 0);	/* no reply on VC III */
		c[0]=KHRN_EGL_DESTROY_GL;
		c[1]=ctx;
		khrn_call(c, 2, 0, 0, 0, 0, 0);
	}
	printf("done\n");
	return 0;
}