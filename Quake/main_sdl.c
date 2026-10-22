/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include <stdio.h>

static void Sys_AtExit (void)
{
	SDL_Quit ();
}

static void Sys_InitSDL (void)
{
	SDL_version  v;
	SDL_version *sdl_version = &v;
	SDL_GetVersion (&v);

	Sys_Printf ("Found SDL version %i.%i.%i\n", sdl_version->major, sdl_version->minor, sdl_version->patch);

	if (SDL_Init (0) < 0)
	{
		Sys_Error ("Couldn't init SDL: %s", SDL_GetError ());
	}

#ifdef _DEBUG
	SDL_LogSetAllPriority (SDL_LOG_PRIORITY_DEBUG);
#endif

	atexit (Sys_AtExit);
}

static quakeparms_t parms;

#ifdef __APPLE__
#include <unistd.h>

// Hang diagnostics: if the main loop stops for 10 seconds, the stacks of all threads are
// sampled (macOS's 'sample' tool) into hang-sample.txt in the game folder, once per run.
static SDL_atomic_t sys_heartbeat;

void Sys_Heartbeat (void)
{
	SDL_AtomicIncRef (&sys_heartbeat);
}

static int SDLCALL Sys_WatchdogThread (void *unused)
{
	int      last = -1;
	double   since = Sys_DoubleTime ();
	qboolean sampled = false;
	(void)unused;

	while (!sampled)
	{
		SDL_Delay (1000);
		const int now = SDL_AtomicGet (&sys_heartbeat);
		if (now != last)
		{
			last = now;
			since = Sys_DoubleTime ();
			continue;
		}
		if (Sys_DoubleTime () - since > 10.0)
		{
			char path[MAX_OSPATH];
			char cmd[MAX_OSPATH + 128];
			sampled = true;
			q_snprintf (path, sizeof (path), "%s/hang-sample.txt", host_parms->basedir);
			fprintf (stderr, "[Watchdog] the game has been stuck for 10 s, saving what each thread is doing to %s\n", path);
			fflush (stderr);
			q_snprintf (cmd, sizeof (cmd), "/usr/bin/sample %d 3 -mayDie -file '%s' >/dev/null 2>&1", (int)getpid (), path);
			const int r = system (cmd);
			fprintf (stderr, "[Watchdog] %s\n", r == 0 ? "saved" : "sampling failed");
			fflush (stderr);
		}
	}
	return 0;
}
#endif

int main (int argc, char *argv[])
{
	double time, oldtime, newtime;

	host_parms = &parms;
	parms.basedir = ".";

	parms.argc = argc;
	parms.argv = argv;

	parms.errstate = 0;

	COM_InitArgv (parms.argc, parms.argv);

	isDedicated = (COM_CheckParm ("-dedicated") != 0);

	Sys_InitSDL ();

	Sys_Init ();

	Sys_Printf ("Initializing vkQuake v%s\n", VKQUAKE_VER_STRING);
#if defined(__clang_version__)
	Sys_Printf ("Built with Clang " __clang_version__ "\n");
#elif defined(__GNUC__)
	Sys_Printf ("Built with GCC %u.%u.%u\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_FULL_VER)
	Sys_Printf ("Built with Microsoft C %u\n", _MSC_FULL_VER);
#else
	Sys_Printf ("Built with unknown compiler\n");
#endif

	Sys_Printf ("Host_Init\n");
	Host_Init ();

#ifdef __APPLE__
	if (!isDedicated)
		SDL_DetachThread (SDL_CreateThread (Sys_WatchdogThread, "watchdog", NULL));
#endif

	oldtime = Sys_DoubleTime ();
	if (isDedicated)
	{
		while (1)
		{
			newtime = Sys_DoubleTime ();
			time = newtime - oldtime;

			while (time < sys_ticrate.value)
			{
				SDL_Delay (1);
				newtime = Sys_DoubleTime ();
				time = newtime - oldtime;
			}

			Host_Frame (time);
			oldtime = newtime;
		}
	}
	else
		while (1)
		{
			/* If we have no input focus at all, sleep a bit */
			if (!VID_HasMouseOrInputFocus () || cl.paused)
			{
				SDL_Delay (16);
			}
			/* If we're minimised, sleep a bit more */
			if (VID_IsMinimized ())
				SDL_Delay (32);
			newtime = Sys_DoubleTime ();
			time = newtime - oldtime;

#ifdef __APPLE__
			Sys_Heartbeat ();
#endif
			Host_Frame (time);

			oldtime = newtime;
		}

	return 0;
}
