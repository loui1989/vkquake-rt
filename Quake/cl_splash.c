/*
Copyright (C) 2026 Quake RT macOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

//
// cl_splash.c -- client-side effects:
//   water splashes where shots and projectiles cross a liquid's surface, sized by
//   what crossed it (cl_watersplash);
//   volumetric smoke clouds left by explosions, rendered by RTGL1 inside the
//   volumetric fog (cl_smoke).
//

#include "quakedef.h"

cvar_t        cl_watersplash = {"cl_watersplash", "1", CVAR_ARCHIVE};
cvar_t        cl_smoke = {"cl_smoke", "1", CVAR_ARCHIVE};
static cvar_t cl_smoke_time = {"cl_smoke_time", "8", CVAR_ARCHIVE};       // seconds a cloud lasts
static cvar_t cl_smoke_density = {"cl_smoke_density", "1", CVAR_ARCHIVE}; // thickness multiplier
static cvar_t cl_smoke_size = {"cl_smoke_size", "1", CVAR_ARCHIVE};       // size multiplier

extern cvar_t      sv_gravity;
extern particle_t *active_particles, *free_particles; // r_part.c

static sfx_t *cl_sfx_splash;
static double last_splash_sound;

static float frand (void)
{
	return (rand () & 0x7fff) / 32767.0f;
}

static float crand (void)
{
	return frand () * 2.0f - 1.0f;
}

static int CL_Contents (const vec3_t p)
{
	if (!cl.worldmodel)
		return CONTENTS_EMPTY;
	return Mod_PointInLeaf ((float *)p, cl.worldmodel)->contents;
}

static qboolean IsLiquid (int contents)
{
	return contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA;
}

// a, b on different sides of a liquid's surface: find the crossing point
static int CL_FindSurface (const vec3_t a, const vec3_t b, vec3_t out)
{
	vec3_t lo, hi, mid;
	VectorCopy (a, lo);
	VectorCopy (b, hi);
	const qboolean loLiquid = IsLiquid (CL_Contents (a));
	int            liquid = loLiquid ? CL_Contents (a) : CL_Contents (b);

	for (int i = 0; i < 12; i++)
	{
		VectorAdd (lo, hi, mid);
		VectorScale (mid, 0.5f, mid);
		const int c = CL_Contents (mid);
		if (IsLiquid (c) == loLiquid)
			VectorCopy (mid, lo);
		else
			VectorCopy (mid, hi);
	}
	VectorAdd (lo, hi, out);
	VectorScale (out, 0.5f, out);
	return liquid;
}

static particle_t *CL_NewParticle (void)
{
	particle_t *p = free_particles;
	if (!p)
		return NULL;
	free_particles = p->next;
	p->next = active_particles;
	active_particles = p;
	return p;
}

/*
=====================================================================
Ripples: rings of waves spreading over the water's surface, rendered by RTGL1
by bending the water's normals (so the ray traced reflections and refraction
wobble with them)
=====================================================================
*/

#define MAX_RIPPLES RG_MAX_WATER_RIPPLES

typedef struct
{
	vec3_t org;
	double start;
	float  size;
	int    used;
} ripple_t;

static ripple_t ripples[MAX_RIPPLES];
static int      ripple_next;

static void CL_AddRipple (const vec3_t surf, float size)
{
	ripple_t *r = &ripples[ripple_next];
	ripple_next = (ripple_next + 1) % MAX_RIPPLES;

	VectorCopy (surf, r->org);
	r->start = cl.time;
	r->size = size;
	r->used = 1;
}

static float RippleDecay (float size)
{
	return 0.6f + 1.0f * sqrtf (size);
}

uint32_t CL_Ripples_Get (RgWaterRipple *out, uint32_t max)
{
	uint32_t n = 0;

	if (!CVAR_TO_FLOAT (cl_watersplash))
		return 0;

	for (int i = 0; i < MAX_RIPPLES && n < max; i++)
	{
		ripple_t *r = &ripples[i];
		if (!r->used)
			continue;

		const float age = (float)(cl.time - r->start);
		const float decay = RippleDecay (r->size);
		if (age < 0.0f || age > decay * 4.0f)
		{
			r->used = 0; // calmed down, or a new map (cl.time restarted)
			continue;
		}

		RgWaterRipple *w = &out[n++];
		VectorCopy (r->org, w->position);
		w->age = age;
		w->strength = CLAMP (0.05f, 0.08f + 0.14f * r->size, 0.5f);
		w->wavelength = 5.0f + 4.0f * r->size;
		w->speed = 28.0f + 14.0f * r->size;
		w->decayTime = decay;
	}
	return n;
}

/*
===============
CL_Splash

A splash at a liquid's surface. size: about 0.3 for a shotgun pellet, 1 for a gib,
1.5-2 for grenades and rockets. entering: thrown up higher than when leaving.
===============
*/
static void CL_Splash (const vec3_t surf, int liquid, float size, qboolean entering, qboolean sound)
{
	int base, range;
	switch (liquid)
	{
	case CONTENTS_SLIME:
		base = 52, range = 8; // greens
		break;
	case CONTENTS_LAVA:
		base = 224, range = 8; // fire
		break;
	default:
		base = 6, range = 10; // light greys: spray
		break;
	}

	size = CLAMP (0.2f, size * CVAR_TO_FLOAT (cl_watersplash), 3.0f);
	const float up = (entering ? 1.0f : 0.6f) * sqrtf (size);

	// the column / crown of droplets
	const int drops = (int)(30 * size);
	for (int i = 0; i < drops; i++)
	{
		particle_t *p = CL_NewParticle ();
		if (!p)
			return;
		const float a = frand () * 2.0f * (float)M_PI;
		const float spread = (i & 1) ? 1.0f : 0.35f; // half a wide crown, half a narrow column
		p->org[0] = surf[0] + cosf (a) * 2.0f * size;
		p->org[1] = surf[1] + sinf (a) * 2.0f * size;
		p->org[2] = surf[2] + 1.0f;
		p->vel[0] = cosf (a) * (30.0f + frand () * 50.0f) * size * spread;
		p->vel[1] = sinf (a) * (30.0f + frand () * 50.0f) * size * spread;
		p->vel[2] = (110.0f + frand () * 170.0f) * up * (spread < 1.0f ? 1.3f : 0.8f);
		p->color = base + (rand () % range);
		p->type = pt_splash;
		p->ramp = 0;
		p->die = cl.time + 0.35 + frand () * 0.5 * sqrtf (size);
	}

	if (liquid != CONTENTS_LAVA)
	{
		// rings of waves on the surface
		CL_AddRipple (surf, size);
	}

	// lava: a ring of embers spreading over the surface
	const int ring = liquid == CONTENTS_LAVA ? (int)(18 * size) : 0;
	for (int i = 0; i < ring; i++)
	{
		particle_t *p = CL_NewParticle ();
		if (!p)
			return;
		const float a = (i + frand () * 0.5f) / ring * 2.0f * (float)M_PI;
		p->org[0] = surf[0] + cosf (a) * 3.0f * size;
		p->org[1] = surf[1] + sinf (a) * 3.0f * size;
		p->org[2] = surf[2] + 0.5f;
		p->vel[0] = cosf (a) * (45.0f + frand () * 25.0f) * size;
		p->vel[1] = sinf (a) * (45.0f + frand () * 25.0f) * size;
		p->vel[2] = 0;
		p->color = base + (rand () % range);
		p->type = pt_splashring;
		p->ramp = 0;
		p->die = cl.time + 0.3 + frand () * 0.2;
	}

	if (sound && cl_sfx_splash && realtime - last_splash_sound > 0.06)
	{
		last_splash_sound = realtime;
		S_StartSound (-1, 0, cl_sfx_splash, (float *)surf, CLAMP (0.15f, 0.3f * size, 1.0f), 1.0f);
	}
}

/*
===============
CL_Splash_Entity

Projectiles, gibs and the player crossing a liquid's surface between two frames.
===============
*/
void CL_Splash_Entity (entity_t *ent, const vec3_t oldorg)
{
	if (!CVAR_TO_FLOAT (cl_watersplash) || !cl.worldmodel || !ent->model || ent->model->type != mod_alias)
		return;

	const char *name = ent->model->name;
	const int   flags = ent->model->flags;
	const qboolean isplayer = ent == &cl.entities[cl.viewentity] || !strcmp (name, "progs/player.mdl");
	float       size;

	if (!strcmp (name, "progs/spike.mdl") || !strcmp (name, "progs/laser.mdl") || !strcmp (name, "progs/k_spike.mdl") ||
	    !strcmp (name, "progs/w_spike.mdl"))
		size = 0.5f;
	else if (!strcmp (name, "progs/s_spike.mdl"))
		size = 0.7f;
	else if (!strcmp (name, "progs/grenade.mdl"))
		size = 1.3f;
	else if (!strcmp (name, "progs/missile.mdl"))
		size = 1.6f;
	else if (!strcmp (name, "progs/lavaball.mdl"))
		size = 1.8f;
	else if (flags & (EF_GIB | EF_ZOMGIB))
		size = 0.8f;
	else if (flags & (EF_ROCKET | EF_GRENADE | EF_TRACER | EF_TRACER2 | EF_TRACER3))
		size = 1.0f; // mission pack / mod projectiles
	else if (isplayer)
		size = 1.6f;
	else
		return;

	vec3_t delta;
	VectorSubtract (ent->origin, oldorg, delta);
	const float dist = VectorLength (delta);
	if (dist < 0.25f || dist > 400.0f) // not moving, or teleported
		return;

	const qboolean wasLiquid = IsLiquid (CL_Contents (oldorg));
	const qboolean isLiquid = IsLiquid (CL_Contents (ent->origin));

	// wading: small ripples around the player's legs while moving in shallow water
	if (ent == &cl.entities[cl.viewentity] && wasLiquid == isLiquid)
	{
		static double last_wade;
		vec3_t        feet, waist, surf;
		VectorCopy (ent->origin, feet);
		feet[2] -= 22.0f;
		VectorCopy (ent->origin, waist);
		waist[2] += 6.0f;
		const float hspeed = sqrtf (delta[0] * delta[0] + delta[1] * delta[1]) / q_max (0.001f, (float)(cl.time - cl.oldtime));
		if (hspeed > 40.0f && cl.time - last_wade > 0.28 && IsLiquid (CL_Contents (feet)) && !IsLiquid (CL_Contents (waist)))
		{
			last_wade = cl.time;
			const int liquid = CL_FindSurface (feet, waist, surf);
			if (liquid != CONTENTS_LAVA)
				CL_AddRipple (surf, CLAMP (0.3f, hspeed / 600.0f, 0.6f));
		}
	}

	if (wasLiquid == isLiquid)
		return;

	const float frametime = q_max (0.001f, (float)(cl.time - cl.oldtime));
	const float speed = dist / frametime;
	if (isplayer)
	{
		// jumping or falling in; not wading around at the surface
		if (!isLiquid || speed < 220.0f || delta[2] > -1.0f)
			return;
		size *= CLAMP (0.6f, speed / 500.0f, 1.3f);
	}
	else
	{
		size *= CLAMP (0.6f, speed / 600.0f, 1.3f);
	}

	vec3_t surf;
	const int liquid = CL_FindSurface (oldorg, ent->origin, surf);
	// the game plays the player's own splash sound
	CL_Splash (surf, liquid, isLiquid ? size : size * 0.5f, isLiquid, !isplayer);
}

/*
===============
CL_Splash_Gunshot

A bullet (shotgun pellet) hit something at pos. Bullets aren't entities, so only the
player's own shots can be followed: from the eye, if pos is roughly where the player aims.
===============
*/
void CL_Splash_Gunshot (const vec3_t pos)
{
	if (!CVAR_TO_FLOAT (cl_watersplash) || !cl.worldmodel)
		return;

	vec3_t eye, dir, fwd, right, up;
	VectorCopy (r_refdef.vieworg, eye);
	VectorSubtract (pos, eye, dir);
	const float len = VectorLength (dir);
	if (len < 8.0f || len > 4096.0f)
		return;
	VectorScale (dir, 1.0f / len, dir);

	AngleVectors (cl.viewangles, fwd, right, up);
	if (DotProduct (dir, fwd) < 0.93f) // ~20 degrees: the double shotgun's spread is ~8
		return;

	vec3_t         prev, cur;
	qboolean       prevLiquid = IsLiquid (CL_Contents (eye));
	const int      steps = (int)ceilf (len / 8.0f);
	VectorCopy (eye, prev);
	for (int k = 1; k <= steps; k++)
	{
		VectorMA (eye, len * k / steps, dir, cur);
		const qboolean l = IsLiquid (CL_Contents (cur));
		if (l != prevLiquid)
		{
			vec3_t    surf;
			const int liquid = CL_FindSurface (prev, cur, surf);
			CL_Splash (surf, liquid, l ? 0.35f : 0.2f, l, true);
			return;
		}
		VectorCopy (cur, prev);
	}
}

/*
=====================================================================
Explosion smoke
=====================================================================
*/

#define MAX_SMOKES RG_MAX_SMOKE_VOLUMES

typedef struct
{
	vec3_t org;
	double start;
	float  size;
	float  seed;
	int    used;
} smoke_t;

static smoke_t smokes[MAX_SMOKES];
static int     smoke_next;

void CL_Smoke_Explosion (const vec3_t pos, float size)
{
	if (!CVAR_TO_BOOL (cl_smoke))
		return;
	if (IsLiquid (CL_Contents (pos)))
		return; // no smoke underwater

	smoke_t *s = &smokes[smoke_next];
	smoke_next = (smoke_next + 1) % MAX_SMOKES;

	VectorCopy (pos, s->org);
	s->start = cl.time;
	s->size = size * CLAMP (0.25f, cl_smoke_size.value, 4.0f);
	s->seed = frand () * 100.0f;
	s->used = 1;
}

uint32_t CL_Smoke_Get (RgSmokeVolume *out, uint32_t max)
{
	uint32_t n = 0;

	if (!CVAR_TO_BOOL (cl_smoke))
		return 0;

	const float duration = CLAMP (1.0f, cl_smoke_time.value, 60.0f);
	const float density = CLAMP (0.0f, cl_smoke_density.value, 10.0f);

	for (int i = 0; i < MAX_SMOKES && n < max; i++)
	{
		smoke_t *s = &smokes[i];
		if (!s->used)
			continue;

		const float age = (float)(cl.time - s->start);
		if (age < 0.0f || age > duration * s->size)
		{
			s->used = 0; // expired, or a new map (cl.time restarted)
			continue;
		}
		const float life = age / (duration * s->size);

		RgSmokeVolume *v = &out[n++];
		// grows quickly at first, then slowly; rises as it cools
		const float grow = 1.0f - expf (-age / 0.7f);
		v->radius = s->size * (28.0f + 60.0f * grow + 18.0f * life);
		v->position[0] = s->org[0];
		v->position[1] = s->org[1];
		v->position[2] = s->org[2] + s->size * (14.0f * age / (1.0f + 0.25f * age)) + 8.0f * grow;
		// thickens over the first moments, then thins out
		const float fadein = CLAMP (0.0f, age / 0.25f, 1.0f);
		const float fadeout = powf (1.0f - life, 1.6f);
		v->density = 0.12f * density * fadein * fadeout / q_max (1.0f, 0.6f + 0.4f * v->radius / (88.0f * s->size));
		v->age = age;
		v->seed = s->seed;
		// the fireball's glow in the first fraction of a second
		v->glow = 0.06f * expf (-age / 0.12f);
	}
	return n;
}

void CL_Splash_Init (void)
{
	Cvar_RegisterVariable (&cl_watersplash);
	Cvar_RegisterVariable (&cl_smoke);
	Cvar_RegisterVariable (&cl_smoke_time);
	Cvar_RegisterVariable (&cl_smoke_density);
	Cvar_RegisterVariable (&cl_smoke_size);

	cl_sfx_splash = S_PrecacheSound ("misc/h2ohit1.wav");
}
