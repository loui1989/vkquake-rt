/*
Copyright (C) 2026 Quake RT macOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef QOL_H
#define QOL_H

// cl_wheel.c -- weapon wheel
extern cvar_t cl_wheel;
void          WHEEL_Init (void);
void          WHEEL_Frame (void);
qboolean      WHEEL_MouseMove (int dx, int dy);
float         WHEEL_TimeScale (void);
void          WHEEL_Draw (cb_context_t *cbx);

// cl_splash.c -- water splashes and explosion smoke
extern cvar_t cl_watersplash;
extern cvar_t cl_smoke;
void          CL_Splash_Init (void);
void          CL_Splash_Entity (entity_t *ent, const vec3_t oldorg);
void          CL_Splash_Gunshot (const vec3_t pos);
void          CL_Smoke_Explosion (const vec3_t pos, float size);
// fills the smoke volumes for this frame, returns their count
uint32_t      CL_Smoke_Get (RgSmokeVolume *out, uint32_t max);

#endif
