#include <SDL2/SDL.h>
#include <iostream>
#include <cassert>
#include <cstdint>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "interpret.hh"
#include "globals.hh"

struct color_t {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};

struct color16 {
  uint16_t b:5;
  uint16_t g:6;
  uint16_t r:5;
};


static int32_t width = 320;
static int32_t height = 200;

static const int32_t fheight = 200;
static const int32_t fwidth = 320;

static SDL_Window *sdlwin = nullptr;
static SDL_Surface *sdlscr = nullptr;

void initializeFB() {
  SDL_Init(SDL_INIT_VIDEO);
  width = fwidth*globals::scale;
  height = fheight*globals::scale;
  sdlwin = SDL_CreateWindow("FRAMEBUFFER",
			    SDL_WINDOWPOS_UNDEFINED,
			    SDL_WINDOWPOS_UNDEFINED,
			    width,
			    height,
			    SDL_WINDOW_SHOWN);
  assert(sdlwin != nullptr);
  sdlscr = SDL_GetWindowSurface(sdlwin);
  assert(sdlscr);
}

void terminateFB() {
  SDL_DestroyWindow(sdlwin);
}


void drawFrame(const state_t *s) {
  SDL_Event e;  
  color16 *in = reinterpret_cast<color16*>(s->mem + globals::fb_phys_addr);  
  SDL_LockSurface(sdlscr);
  color_t *out = reinterpret_cast<color_t*>(sdlscr->pixels);
  
  /* memcpy(out,in,sizeof(color_t)*height*width); */

  for(int i = 0; i < fheight; i++) {
    for(int ii = 0; ii < globals::scale; ii++) {
      int h = i*globals::scale + ii;
      for(int j = 0; j < fwidth; j++) {
	color16 c = in[i*fwidth+j];
	color_t p;
	p.b = c.b * (256/32);
	p.g = c.g * (256/64);
	p.r = c.r * (256/32);
	for(int jj = 0; jj < globals::scale; jj++) {
	  int w = j*globals::scale + jj;
	  out[h*width + w] = p;
	}
      }
    }
  }
    
#if 0
  for(int i = 0; i < (height*width); i++) {
    out[i].b = in[i].b * (256/32);
    out[i].g = in[i].g * (256/64);
    out[i].r = in[i].r * (256/32);    
  }
#endif
    
  
  SDL_UnlockSurface(sdlscr);
  SDL_UpdateWindowSurface(sdlwin);

  while(SDL_PollEvent(&e)) {
    switch(e.type)
      {
      default:
	break;
      }
  }
  
}
