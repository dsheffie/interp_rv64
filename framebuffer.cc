#include <SDL2/SDL.h>
#include <iostream>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <sstream>
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


static SDL_Window *sdlwin = nullptr;
static SDL_Surface *sdlscr = nullptr;

void initializeFB() {
  SDL_Init(SDL_INIT_VIDEO);
  width = globals::fwidth*globals::scale;
  height = globals::fheight*globals::scale;
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

typedef uint8_t Rgb[3];

static int frame_id = 0;
void drawFrame(const state_t *s) {
  SDL_Event e;  
  color16 *in = reinterpret_cast<color16*>(s->mem + globals::fb_phys_addr);  
  SDL_LockSurface(sdlscr);
  color_t *out = reinterpret_cast<color_t*>(sdlscr->pixels);
  

  const int sc = globals::scale;
  const int w = globals::fwidth;
  const int h = globals::fheight;

#if 0
  Rgb *framebuffer = new Rgb[w*h];
  std::ofstream ofs;
  std::stringstream ss;
  ss << "frame_" << frame_id << ".ppm";
  ofs.open(ss.str());
  ofs << "P6\n" << w << " " << h << "\n255\n";
  
  for(int j = 0; j < (w*h); j++) {
    color16 pix = in[j];
    framebuffer[j][0] = 8 * pix.r;
    framebuffer[j][1] = 4 * pix.g;
    framebuffer[j][2] = 8 * pix.b;
  }
  ofs.write((char*)framebuffer, w * h * 3);
  ofs.close();
  delete [] framebuffer;
#endif
  ++frame_id;

  
  for(int i = 0; i < h; i++) {
    for(int ii = 0; ii < sc; ii++) {
      int h = i*sc + ii;
      for(int j = 0; j < w; j++) {
	color16 c = in[i*w+j];
	color_t p;
	p.b = c.b * (256/32);
	p.g = c.g * (256/64);
	p.r = c.r * (256/32);
	for(int jj = 0; jj < sc; jj++) {
	  int w_ = j*sc + jj;
	  out[h*width + w_] = p;
	}
      }
    }
  }
    
  
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
