// Smooth gradients: the tone pass written with a one-code dither (docs/bloom-artifact-2026-10-20.md).
//
// WHAT. The game writes its tonemapped picture to an 8-bit target with no dither, so a dim glow steps from one code value to
// the next and reads as bands, and so does a dark gradient. The fix gives that one write a faint triangular dither of one
// code: the steps break up into grain too fine to see, and the gradients come out smooth. The glow's size, brightness and
// shape do not change, and black stays black.
//
// HOW. The game's own tone draw is given a transcription of its shader with the dither added (smooth_gradients_ps.h).
// Begin binds it for that one draw, and End puts the game's shader back straight after the draw. The HUD is re-issued
// after that with the game's own shader, so the dither never reaches the interface. Only the five tone shaders EDVR has
// transcribed are replaced; any other full-screen 8-bit draw is counted and left stock.
//
// WHERE. The flat profile only, for now. The VR tone shader has not been transcribed, so VR reads the key and draws stock.
// fix.smooth_gradients turns it on and off, live.
#pragma once

#include <cstdint>

struct ID3D11DeviceContext;

namespace edvr {

class Config;

// Reads fix.smooth_gradients and the profile. Install and reload; live.
void smoothGradientsConfigure(Config& cfg);

// Binds the dithered tone shader for one game draw, when this draw is one of the five tone passes the fix covers. True means
// the replacement is bound and the caller must draw and then call smoothGradientsEnd. False means nothing was touched. Owner
// context only: the caller checks that, and the shader hash it reads is the owner's.
bool smoothGradientsBegin(ID3D11DeviceContext* ctx, char kind, uint32_t count, uint32_t instances, uint32_t startInstance);

// Restores the game's pixel shader. Always paired with a true smoothGradientsBegin.
void smoothGradientsEnd(ID3D11DeviceContext* ctx);

void smoothGradientsShutdown();

}  // namespace edvr
