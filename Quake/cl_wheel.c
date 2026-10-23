/*
Copyright (C) 2026 Quake RT macOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

//
// cl_wheel.c -- weapon wheel (as in the Quake II port)
//
// Hold the key bound to "+wheel" (R and the middle mouse button by default), move the
// mouse towards a weapon and release the key to switch to it; releasing near the centre
// cancels. In local single player the game slows down while the wheel is open.
//
// cl_wheel: 0 - off, 1 - on with slow motion, 2 - on without slow motion
//

#include "quakedef.h"

cvar_t cl_wheel = {"cl_wheel", "1", CVAR_ARCHIVE};
cvar_t cl_wheel_timescale = {"cl_wheel_timescale", "0.25", CVAR_ARCHIVE};
cvar_t cl_wheel_sensitivity = {"cl_wheel_sensitivity", "1", CVAR_ARCHIVE};
// set once R / MOUSE3 got their default "+wheel" bindings (only if they were free)
static cvar_t cl_wheel_defaultbinds = {"_cl_wheel_defaultbinds", "0", CVAR_ARCHIVE};

extern qpic_t *sb_weapons[7][8]; // sbar.c: [0] active, [1] owned

typedef struct
{
	int         impulse;
	const char *label;
	int         item;     // IT_*
	int         ammostat; // STAT_*, -1 = none
	int         ammoneed;
	int         pic;      // index into sb_weapons, -1 = none (axe)
} wheel_weapon_t;

static const wheel_weapon_t wheel_weapons[] = {
	{1, "Axe", IT_AXE, -1, 0, -1},
	{2, "Shotgun", IT_SHOTGUN, STAT_SHELLS, 1, 0},
	{3, "Double Shotgun", IT_SUPER_SHOTGUN, STAT_SHELLS, 2, 1},
	{4, "Nailgun", IT_NAILGUN, STAT_NAILS, 1, 2},
	{5, "Super Nailgun", IT_SUPER_NAILGUN, STAT_NAILS, 2, 3},
	{6, "Grenade Launcher", IT_GRENADE_LAUNCHER, STAT_ROCKETS, 1, 4},
	{7, "Rocket Launcher", IT_ROCKET_LAUNCHER, STAT_ROCKETS, 1, 5},
	{8, "Thunderbolt", IT_LIGHTNING, STAT_CELLS, 1, 6},
};

#define WHEEL_COUNT ((int)countof (wheel_weapons))
#define WHEEL_PI    3.14159265358979323846f

static struct
{
	qboolean active;
	float    cx, cy;   // virtual cursor relative to the centre, -1..1
	int      selected; // index into wheel_weapons, -1 = none
	double   open_time;
	float    timescale;
} wheel = {.timescale = 1.0f};

static qboolean WHEEL_Owned (int i)
{
	return (cl.items & wheel_weapons[i].item) != 0;
}

static qboolean WHEEL_Usable (int i)
{
	return WHEEL_Owned (i) && (wheel_weapons[i].ammostat < 0 || cl.stats[wheel_weapons[i].ammostat] >= wheel_weapons[i].ammoneed);
}

// angle of slot i, clockwise from 12 o'clock
static float WHEEL_SlotAngle (int i)
{
	return -WHEEL_PI / 2 + (2 * WHEEL_PI) * i / WHEEL_COUNT;
}

static void WHEEL_UpdateSelection (void)
{
	const float len = sqrtf (wheel.cx * wheel.cx + wheel.cy * wheel.cy);
	float       best = 999.0f;

	wheel.selected = -1;
	if (len < 0.25f)
		return;

	const float ang = atan2f (wheel.cy, wheel.cx);
	for (int i = 0; i < WHEEL_COUNT; i++)
	{
		if (!WHEEL_Owned (i))
			continue;
		const float d = fabsf (remainderf (ang - WHEEL_SlotAngle (i), 2 * WHEEL_PI));
		if (d < best)
		{
			best = d;
			wheel.selected = i;
		}
	}
}

static qboolean WHEEL_CanOpen (void)
{
	return CVAR_TO_INT32 (cl_wheel) != 0 && cls.state == ca_connected && cls.signon == SIGNONS && !cls.demoplayback &&
	       key_dest == key_game && !cl.intermission && cl.stats[STAT_HEALTH] > 0;
}

// slow motion only in local single player: the whole local game is slowed
static qboolean WHEEL_SlowmoAllowed (void)
{
	return CVAR_TO_INT32 (cl_wheel) == 1 && sv.active && svs.maxclients == 1 && !cls.demoplayback;
}

static void WHEEL_Close (qboolean select)
{
	if (!wheel.active)
		return;
	wheel.active = false;

	if (select && wheel.selected >= 0 && WHEEL_Owned (wheel.selected))
		Cbuf_AddText (va ("impulse %d\n", wheel_weapons[wheel.selected].impulse));
}

static void WHEEL_Down_f (void)
{
	if (!WHEEL_CanOpen ())
		return;
	wheel.active = true;
	wheel.cx = wheel.cy = 0;
	wheel.selected = -1;
	wheel.open_time = Sys_DoubleTime ();
}

static void WHEEL_Up_f (void)
{
	WHEEL_Close (true);
}

qboolean WHEEL_MouseMove (int dx, int dy)
{
	if (!wheel.active)
		return false;

	const float scale = CLAMP (0.05f, cl_wheel_sensitivity.value, 20.0f) * 0.004f;
	wheel.cx += dx * scale;
	wheel.cy += dy * scale;

	// keep the cursor on the ring so that direction changes stay responsive
	const float len = sqrtf (wheel.cx * wheel.cx + wheel.cy * wheel.cy);
	if (len > 1.0f)
	{
		wheel.cx /= len;
		wheel.cy /= len;
	}
	WHEEL_UpdateSelection ();
	return true;
}

float WHEEL_TimeScale (void)
{
	return wheel.timescale;
}

static void WHEEL_DefaultBinds (void)
{
	static const int keys[] = {'r', K_MOUSE3};

	if (cl_wheel_defaultbinds.value)
		return;
	for (int i = 0; i < (int)countof (keys); i++)
	{
		if (!keybindings[keys[i]] || !keybindings[keys[i]][0])
			Key_SetBinding (keys[i], "+wheel");
	}
	Cvar_SetValueQuick (&cl_wheel_defaultbinds, 1);
}

// main thread, every frame
void WHEEL_Frame (void)
{
	// after config.cfg has been executed
	if (host_framecount > 2)
		WHEEL_DefaultBinds ();

	if (wheel.active && !WHEEL_CanOpen ())
		WHEEL_Close (false);

	if (wheel.active)
		WHEEL_UpdateSelection (); // the inventory may change while it's open

	float target = 1.0f;
	if (wheel.active && WHEEL_SlowmoAllowed ())
	{
		// ease in over 150 ms of real time
		const float t = CLAMP (0.0f, (float)(Sys_DoubleTime () - wheel.open_time) / 0.15f, 1.0f);
		target = 1.0f + (CLAMP (0.05f, cl_wheel_timescale.value, 1.0f) - 1.0f) * t;
	}
	wheel.timescale = target;
}

void WHEEL_Draw (cb_context_t *cbx)
{
	if (!wheel.active)
		return;

	// dim the scene
	GL_SetCanvas (cbx, CANVAS_DEFAULT);
	Draw_Fill (cbx, 0, 0, glwidth, glheight, 0, 0.35f);

	GL_SetCanvas (cbx, CANVAS_MENU); // 320x200, scaled with the menus
	const int   cx = 160, cy = 100;
	const float radius = 72.0f;

	int active = -1;
	for (int i = 0; i < WHEEL_COUNT; i++)
		if (cl.stats[STAT_ACTIVEWEAPON] == wheel_weapons[i].item)
			active = i;

	for (int i = 0; i < WHEEL_COUNT; i++)
	{
		const float a = WHEEL_SlotAngle (i);
		const int   sx = cx + (int)roundf (cosf (a) * radius);
		const int   sy = cy + (int)roundf (sinf (a) * radius);
		const int   bw = 40, bh = 26;
		const int   bx = sx - bw / 2, by = sy - bh / 2;

		const qboolean owned = WHEEL_Owned (i);
		const qboolean usable = WHEEL_Usable (i);
		const qboolean sel = i == wheel.selected;

		// slot background: highlighted when selected, a bit lighter for the weapon in hand
		Draw_Fill (cbx, bx, by, bw, bh, sel ? 192 : (i == active ? 8 : 0), sel ? 0.6f : 0.55f);

		if (wheel_weapons[i].pic >= 0 && sb_weapons[1][wheel_weapons[i].pic])
		{
			qpic_t *pic = sb_weapons[sel || i == active ? 0 : 1][wheel_weapons[i].pic];
			if (!pic)
				pic = sb_weapons[1][wheel_weapons[i].pic];
			Draw_Pic (cbx, sx - pic->width / 2, sy - pic->height / 2, pic, owned ? (usable ? 1.0f : 0.45f) : 0.12f, true);
		}
		else
		{
			// no icon (axe): its name
			const char *s = wheel_weapons[i].label;
			if (owned)
				Draw_String (cbx, sx - (int)strlen (s) * 4, sy - 4, s);
		}

		// the weapon's number, in the slot's corner
		if (owned)
			Draw_Character (cbx, bx + 2, by + 2, '0' + wheel_weapons[i].impulse + (sel ? 0 : 128));
	}

	// caption in the centre
	const char *label = wheel.selected >= 0 ? wheel_weapons[wheel.selected].label : "Cancel";
	Draw_String (cbx, cx - (int)strlen (label) * 4, cy - 8, label);
	if (wheel.selected >= 0 && !WHEEL_Usable (wheel.selected))
	{
		const char *note = "no ammo";
		for (int c = 0; note[c]; c++)
			Draw_Character (cbx, cx - (int)strlen (note) * 4 + c * 8, cy + 2, note[c] + 128);
	}
}

void WHEEL_Init (void)
{
	Cvar_RegisterVariable (&cl_wheel);
	Cvar_RegisterVariable (&cl_wheel_timescale);
	Cvar_RegisterVariable (&cl_wheel_sensitivity);
	Cvar_RegisterVariable (&cl_wheel_defaultbinds);

	Cmd_AddCommand ("+wheel", WHEEL_Down_f);
	Cmd_AddCommand ("-wheel", WHEEL_Up_f);
}
