// Minimal SDL_ttf declarations for host-side syntax checking ONLY (tests/host/run.sh).
#pragma once
#include "SDL.h"

typedef struct _TTF_Font TTF_Font;
int TTF_Init(void);
void TTF_Quit(void);
TTF_Font* TTF_OpenFont(const char* file, int ptsize);
void TTF_CloseFont(TTF_Font* f);
int TTF_FontHeight(const TTF_Font* f);
int TTF_SizeUTF8(TTF_Font* f, const char* text, int* w, int* h);
SDL_Surface* TTF_RenderUTF8_Blended(TTF_Font* f, const char* text, SDL_Color fg);
