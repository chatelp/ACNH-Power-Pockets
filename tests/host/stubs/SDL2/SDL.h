// Minimal SDL2 declarations for host-side syntax checking ONLY (tests/host/run.sh).
#pragma once
#include <cstdint>

typedef uint8_t Uint8; typedef uint32_t Uint32;
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Surface { int w, h; } SDL_Surface;
struct SDL_Renderer; struct SDL_Texture; struct SDL_Window;
typedef enum { SDL_BLENDMODE_NONE = 0, SDL_BLENDMODE_BLEND = 1 } SDL_BlendMode;
enum { SDL_INIT_VIDEO = 0x20 };
enum { SDL_WINDOW_FULLSCREEN = 1 };
enum { SDL_RENDERER_ACCELERATED = 2, SDL_RENDERER_PRESENTVSYNC = 4 };
enum { SDL_PIXELFORMAT_RGBA32 = 0x16762004 };
enum { SDL_TEXTUREACCESS_STATIC = 0 };

int SDL_Init(Uint32 flags);
void SDL_Quit(void);
SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window* w);
SDL_Renderer* SDL_CreateRenderer(SDL_Window* w, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer* r);
int SDL_SetRenderDrawColor(SDL_Renderer* r, Uint8 R, Uint8 G, Uint8 B, Uint8 A);
int SDL_SetRenderDrawBlendMode(SDL_Renderer* r, SDL_BlendMode m);
int SDL_RenderFillRect(SDL_Renderer* r, const SDL_Rect* rect);
int SDL_RenderClear(SDL_Renderer* r);
int SDL_RenderCopy(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst);
void SDL_RenderPresent(SDL_Renderer* r);
SDL_Texture* SDL_CreateTexture(SDL_Renderer* r, Uint32 fmt, int access, int w, int h);
SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer* r, SDL_Surface* s);
int SDL_UpdateTexture(SDL_Texture* t, const SDL_Rect* rect, const void* px, int pitch);
int SDL_SetTextureBlendMode(SDL_Texture* t, SDL_BlendMode m);
int SDL_SetTextureColorMod(SDL_Texture* t, Uint8 r, Uint8 g, Uint8 b);
int SDL_QueryTexture(SDL_Texture* t, Uint32* fmt, int* access, int* w, int* h);
void SDL_DestroyTexture(SDL_Texture* t);
void SDL_FreeSurface(SDL_Surface* s);
Uint32 SDL_GetTicks(void);
