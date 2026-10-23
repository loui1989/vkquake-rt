/*
Copyright (C) 2026 Quake RT macOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

//
// cl_decals.c -- bullet holes, nail marks and explosion scorch marks on the world,
// drawn by RTGL1 as decals (they change the surface's colour, so they're lit like it).
// The textures are generated here. Marks stay until the map changes; the oldest are
// replaced when there are too many.
//

#include "quakedef.h"

cvar_t        cl_decals = {"cl_decals", "1", CVAR_ARCHIVE};
static cvar_t cl_decals_max = {"cl_decals_max", "512", CVAR_ARCHIVE};

#define MAX_DECALS 1024

typedef enum
{
	DECAL_BULLET,
	DECAL_NAIL,
	DECAL_SCORCH,
	DECAL_TYPES
} decaltype_t;

typedef struct
{
	RgTransform transform;
	decaltype_t type;
} decal_t;

static decal_t     decals[MAX_DECALS];
static int         decal_next, decal_count;
static RgMaterial  decal_materials[DECAL_TYPES];
static qboolean    decal_materials_made;

/*
=====================================================================
Textures
=====================================================================
*/

static float hash2 (int x, int y, int seed)
{
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return (float)((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

// smooth value noise in [0, 1]
static float vnoise (float x, float y, int seed)
{
	const int   ix = (int)floorf (x), iy = (int)floorf (y);
	float       fx = x - ix, fy = y - iy;
	fx = fx * fx * (3 - 2 * fx);
	fy = fy * fy * (3 - 2 * fy);
	const float a = hash2 (ix, iy, seed), b = hash2 (ix + 1, iy, seed);
	const float c = hash2 (ix, iy + 1, seed), d = hash2 (ix + 1, iy + 1, seed);
	return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static float fbm (float x, float y, int seed)
{
	return 0.5f * vnoise (x, y, seed) + 0.25f * vnoise (x * 2.1f, y * 2.1f, seed + 1) + 0.125f * vnoise (x * 4.3f, y * 4.3f, seed + 2);
}

static byte to_byte (float v)
{
	return (byte)CLAMP (0.0f, v * 255.0f + 0.5f, 255.0f);
}

// a hole: black centre, a dark, ragged scorched ring with a few streaks around it
static void MakeHole (byte *rgba, int size, int seed, float holeRadius, float streaks)
{
	for (int y = 0; y < size; y++)
	{
		for (int x = 0; x < size; x++)
		{
			const float u = (x + 0.5f) / size - 0.5f, v = (y + 0.5f) / size - 0.5f;
			const float r = sqrtf (u * u + v * v) * 2.0f; // 0 centre .. 1 edge
			const float ang = atan2f (v, u);

			// ragged edge, and streaks radiating outwards
			const float edge = 0.55f + 0.25f * fbm (cosf (ang) * 3 + 10, sinf (ang) * 3 + 10, seed);
			const float streak = streaks * powf (vnoise (ang * 3.0f + 20, 0.5f, seed + 7), 4.0f);
			const float reach = edge + streak * 0.4f;

			float alpha = 1.0f - CLAMP (0.0f, (r - holeRadius) / q_max (0.01f, reach - holeRadius), 1.0f);
			alpha = alpha * alpha * (0.75f + 0.25f * fbm (u * 14, v * 14, seed + 3));
			float shade = 0.18f + 0.1f * fbm (u * 10, v * 10, seed + 4);
			if (r < holeRadius)
			{
				alpha = 1.0f;
				shade = 0.02f; // the hole itself
			}
			else if (r < holeRadius * 1.6f)
			{
				shade *= 0.5f; // its scorched lip
			}

			byte *p = rgba + (y * size + x) * 4;
			p[0] = to_byte (shade);
			p[1] = to_byte (shade * 0.95f);
			p[2] = to_byte (shade * 0.9f);
			p[3] = to_byte (CLAMP (0.0f, alpha, 1.0f) * (r > 1.0f ? 0.0f : 1.0f));
		}
	}
}

// soot: a blotchy, dark, roughly round stain, densest in the middle
static void MakeScorch (byte *rgba, int size, int seed)
{
	for (int y = 0; y < size; y++)
	{
		for (int x = 0; x < size; x++)
		{
			const float u = (x + 0.5f) / size - 0.5f, v = (y + 0.5f) / size - 0.5f;
			const float r = sqrtf (u * u + v * v) * 2.0f;
			const float ang = atan2f (v, u);

			const float blotch = fbm (u * 6 + 3, v * 6 + 3, seed);
			const float edge = 0.6f + 0.35f * fbm (cosf (ang) * 2.5f + 5, sinf (ang) * 2.5f + 5, seed + 1);
			float       alpha = CLAMP (0.0f, 1.0f - r / edge, 1.0f);
			alpha = powf (alpha, 0.7f) * (0.55f + 0.6f * blotch);
			const float shade = 0.035f + 0.05f * fbm (u * 20, v * 20, seed + 2);

			byte *p = rgba + (y * size + x) * 4;
			p[0] = to_byte (shade);
			p[1] = to_byte (shade * 0.92f);
			p[2] = to_byte (shade * 0.85f);
			p[3] = to_byte (CLAMP (0.0f, alpha * 0.9f, 1.0f));
		}
	}
}

static RgMaterial CreateDecalMaterial (const byte *rgba, int size)
{
	RgMaterialCreateInfo info = {
		.flags = 0,
		.size = {.width = (uint32_t)size, .height = (uint32_t)size},
		.textures = {.pDataAlbedoAlpha = rgba},
		.pRelativePath = NULL,
		.filter = RG_SAMPLER_FILTER_LINEAR,
		.addressModeU = RG_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = RG_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};
	RgMaterial m = RG_NO_MATERIAL;
	RgResult   r = rgCreateMaterial (vulkan_globals.instance, &info, &m);
	return r == RG_SUCCESS ? m : RG_NO_MATERIAL;
}

static void CL_Decals_MakeMaterials (void)
{
	if (decal_materials_made)
		return;
	decal_materials_made = true;

	enum { HOLE = 64, SCORCH = 128 };
	byte *buf = Mem_Alloc (SCORCH * SCORCH * 4);

	MakeHole (buf, HOLE, 11, 0.16f, 1.0f);
	decal_materials[DECAL_BULLET] = CreateDecalMaterial (buf, HOLE);

	MakeHole (buf, HOLE, 23, 0.22f, 0.3f);
	decal_materials[DECAL_NAIL] = CreateDecalMaterial (buf, HOLE);

	MakeScorch (buf, SCORCH, 37);
	decal_materials[DECAL_SCORCH] = CreateDecalMaterial (buf, SCORCH);

	Mem_Free (buf);
}

/*
=====================================================================
Placement
=====================================================================
*/

static float frand (void)
{
	return (rand () & 0x7fff) / 32767.0f;
}

// the world surface nearest to pos (within maxdist), preferring direction 'pref' if given
static qboolean FindSurface (const vec3_t pos, const vec3_t pref, float maxdist, vec3_t outpos, vec3_t outnormal)
{
	static const vec3_t dirs[] = {
		{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
	};
	float best = 2.0f;
	int   entnum;

	if (!cl.worldmodel)
		return false;

	if (pref)
	{
		vec3_t start, end, impact, normal;
		VectorMA (pos, -4.0f, pref, start);
		VectorMA (pos, maxdist, pref, end);
		const float f = CL_TraceLine (start, end, impact, normal, &entnum);
		if (f < 1.0f && entnum == 0)
		{
			VectorCopy (impact, outpos);
			VectorCopy (normal, outnormal);
			return true;
		}
	}

	for (int i = 0; i < (int)countof (dirs); i++)
	{
		vec3_t end, impact, normal;
		VectorMA (pos, maxdist, dirs[i], end);
		const float f = CL_TraceLine ((float *)pos, end, impact, normal, &entnum);
		// only the world: doors and lifts move
		if (f < best && entnum == 0)
		{
			best = f;
			VectorCopy (impact, outpos);
			VectorCopy (normal, outnormal);
		}
	}
	return best <= 1.0f;
}

static void AddDecal (const vec3_t pos, const vec3_t normal, float size, float depth, decaltype_t type)
{
	vec3_t n, t, b, up;
	VectorCopy (normal, n);
	VectorNormalize (n);

	// a random rotation around the normal
	if (fabsf (n[2]) < 0.9f)
		VectorSet (up, 0, 0, 1);
	else
		VectorSet (up, 1, 0, 0);
	CrossProduct (up, n, t);
	VectorNormalize (t);
	CrossProduct (n, t, b);
	const float a = frand () * 2.0f * (float)M_PI, ca = cosf (a), sa = sinf (a);
	vec3_t      tr, br;
	for (int i = 0; i < 3; i++)
	{
		tr[i] = t[i] * ca + b[i] * sa;
		br[i] = b[i] * ca - t[i] * sa;
	}

	decal_t *d = &decals[decal_next];
	const int maxd = CLAMP (16, CVAR_TO_INT32 (cl_decals_max), MAX_DECALS);
	decal_next = (decal_next + 1) % maxd;
	if (decal_count < maxd)
		decal_count++;

	// columns: the box's x, y (along the surface) and z (the normal) axes, then its centre
	for (int i = 0; i < 3; i++)
	{
		d->transform.matrix[i][0] = tr[i] * size;
		d->transform.matrix[i][1] = br[i] * size;
		d->transform.matrix[i][2] = n[i] * depth;
		d->transform.matrix[i][3] = pos[i];
	}
	d->type = type;
}

void CL_Decal_Impact (const vec3_t pos, int kind)
{
	if (!CVAR_TO_BOOL (cl_decals) || CVAR_TO_BOOL (rt_classic_render))
		return;

	vec3_t surf, normal, fwd, right, up, dir;
	float *pref = NULL;

	// the player's own shots: along the aim, for the right spot on slanted surfaces
	AngleVectors (cl.viewangles, fwd, right, up);
	VectorSubtract (pos, r_refdef.vieworg, dir);
	const float len = VectorNormalize (dir);
	if (len > 8.0f && DotProduct (dir, fwd) > 0.93f)
		pref = dir;

	if (kind == DECAL_SCORCH)
	{
		if (!FindSurface (pos, NULL, 40.0f, surf, normal))
			return;
		AddDecal (surf, normal, 56.0f + frand () * 16.0f, 24.0f, DECAL_SCORCH);
	}
	else
	{
		if (!FindSurface (pos, pref, 12.0f, surf, normal))
			return;
		const float size = kind == DECAL_NAIL ? 4.0f + frand () : 5.0f + frand () * 1.5f;
		AddDecal (surf, normal, size, 4.0f, (decaltype_t)kind);
	}
}

// between rgStartFrame and rgDrawFrame
void CL_Decals_Upload (void)
{
	if (!CVAR_TO_BOOL (cl_decals) || CVAR_TO_BOOL (rt_classic_render) || !decal_materials_made)
		return;

	for (int i = 0; i < decal_count; i++)
	{
		const decal_t *d = &decals[i];
		if (decal_materials[d->type] == RG_NO_MATERIAL)
			continue;
		RgDecalUploadInfo info = {
			.transform = d->transform,
			.material = decal_materials[d->type],
		};
		RgResult r = rgUploadDecal (vulkan_globals.instance, &info);
		RG_CHECK (r);
	}
}

// a new map: no marks. The renderer is idle here (map loading), so the materials
// are made now rather than in the middle of a frame.
void CL_Decals_NewMap (void)
{
	decal_next = decal_count = 0;
	CL_Decals_MakeMaterials ();
}

void CL_Decals_Init (void)
{
	Cvar_RegisterVariable (&cl_decals);
	Cvar_RegisterVariable (&cl_decals_max);
}
