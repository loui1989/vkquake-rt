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
extern cvar_t cl_gunsmoke;
void          CL_Splash_Init (void);
void          CL_Splash_Entity (entity_t *ent, const vec3_t oldorg);
void          CL_Splash_Gunshot (const vec3_t pos);
void          CL_Smoke_Explosion (const vec3_t pos, float size);
// your gun fired: smoke out of its muzzle, if it's a gun that would make any (weapon: IT_*)
void          CL_Smoke_Muzzle (const vec3_t muzzle, const vec3_t forward, const vec3_t velocity, int weapon);
extern qboolean cl_gunsmoke_pending; // your own gun fired (set when parsing, used in CL_RelinkEntities)
// fills the smoke volumes for this frame, returns their count
uint32_t      CL_Smoke_Get (RgSmokeVolume *out, uint32_t max);
// fills the water ripples for this frame, returns their count
uint32_t      CL_Ripples_Get (RgWaterRipple *out, uint32_t max);

// cl_decals.c -- bullet holes, nail marks, explosion scorch marks
extern cvar_t cl_decals;
// host_cmd.c -- respawning after death in single player loads the latest save
extern cvar_t cl_deathload;
extern cvar_t scr_showfps;
void          CL_Decals_Init (void);
void          CL_Decals_NewMap (void);
void          CL_Decals_Upload (void);
void          CL_Decal_Impact (const vec3_t pos, int kind); // 0 bullet, 1 nail, 2 scorch

#endif
