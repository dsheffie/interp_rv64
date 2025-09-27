#include <SDL2/SDL.h>
#include <iostream>
#include <cassert>
#include <cstdint>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "interpret.hh"

struct color_t {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};

const int32_t width = 320;
const int32_t height = 200;
static SDL_Window *sdlwin = nullptr;
static SDL_Surface *sdlscr = nullptr;

void initializeFB() {
  SDL_Init(SDL_INIT_VIDEO); 
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

static const uint64_t phys_offs = 0x6000000;
void drawFrame(const state_t *s) {
  SDL_Event e;  
  color_t *in = reinterpret_cast<color_t*>(s->mem + phys_offs);  
  SDL_LockSurface(sdlscr);
  color_t *out = reinterpret_cast<color_t*>(sdlscr->pixels);
  memcpy(out,in,sizeof(color_t)*height*width);
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
