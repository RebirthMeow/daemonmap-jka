/* -------------------------------------------------------------------------------

   Copyright (C) 1999-2007 id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

   ----------------------------------------------------------------------------------

   This code has been altered significantly from its original form, to support
   several games based on the Quake III Arena engine, in the form of "Q3Map2."

   ------------------------------------------------------------------------------- */



/* marker */
#ifndef GAME_JA_H
#define GAME_JA_H



/* -------------------------------------------------------------------------------

   content and surface flags
   (derived from JK2/JKA source)

   ------------------------------------------------------------------------------- */

#define JA_CONT_SOLID              1
#define JA_CONT_LAVA               8
#define JA_CONT_SLIME              16
#define JA_CONT_WATER              32
#define JA_CONT_FOG                64

#define JA_CONT_AREAPORTAL         0x8000

#define JA_CONT_PLAYERCLIP         0x10000
#define JA_CONT_MONSTERCLIP        0x20000
#define JA_CONT_TELEPORTER         0x40000
#define JA_CONT_JUMPPAD           0x80000
#define JA_CONT_CLUSTERPORTAL      0x100000
#define JA_CONT_DONOTENTER         0x200000
#define JA_CONT_BOTCLIP            0x400000

#define JA_CONT_ORIGIN             0x1000000
#define JA_CONT_BODY               0x2000000
#define JA_CONT_CORPSE             0x4000000
#define JA_CONT_DETAIL             0x8000000
#define JA_CONT_STRUCTURAL         0x10000000
#define JA_CONT_TRANSLUCENT        0x20000000
#define JA_CONT_TRIGGER            0x40000000
#define JA_CONT_NODROP             0x80000000

#define JA_SURF_NODAMAGE           0x1
#define JA_SURF_SLICK              0x2
#define JA_SURF_SKY                0x4
#define JA_SURF_LADDER             0x8
#define JA_SURF_NOIMPACT           0x10
#define JA_SURF_NOMARKS            0x20
#define JA_SURF_FLESH              0x40
#define JA_SURF_NODRAW             0x80
#define JA_SURF_HINT               0x100
#define JA_SURF_SKIP               0x200
#define JA_SURF_NOLIGHTMAP         0x400
#define JA_SURF_POINTLIGHT         0x800
#define JA_SURF_METALSTEPS         0x1000
#define JA_SURF_NOSTEPS            0x2000
#define JA_SURF_NONSOLID           0x4000
#define JA_SURF_LIGHTFILTER        0x8000
#define JA_SURF_ALPHASHADOW        0x10000
#define JA_SURF_NODLIGHT           0x20000
#define JA_SURF_FORCEFIELD         0x40000


/* -------------------------------------------------------------------------------

   game_t struct

   ------------------------------------------------------------------------------- */

{
	"ja",               /* -game x */
	"base",             /* default base game data dir */
	".ja",              /* unix home sub-dir */
	"jk2",              /* magic path word */
	"scripts",          /* shader directory */
	64,                 /* max lightmapped surface verts */
	999,                /* max surface verts */
	6000,               /* max surface indexes */
	qfalse,             /* enable per shader prefix surface flags and .tex file */
	qfalse,             /* flares */
	"flareshader",      /* default flare shader */
	qfalse,             /* wolf lighting model? */
	128,                /* lightmap width/height */
	1.0f,               /* lightmap gamma */
	qfalse,             /* lightmap sRGB */
	qfalse,             /* texture sRGB */
	qfalse,             /* color sRGB */
	0.0f,               /* lightmap exposure */
	1.0f,               /* lightmap compensate */
	1.0f,               /* lightgrid scale */
	1.0f,               /* lightgrid ambient scale */
	qfalse,             /* light angle attenuation uses half-lambert curve */
	qfalse,             /* disable shader lightstyles hack */
	qfalse,             /* keep light entities on bsp */
	8,                  /* default patchMeta subdivisions tolerance */
	qfalse,             /* patch casting enabled */
	qfalse,             /* compile deluxemaps */
	0,                  /* deluxemaps default mode */
	512,                /* minimap size */
	1.0f,               /* minimap sharpener */
	0.0f,               /* minimap border */
	qtrue,              /* minimap keep aspect */
	MINIMAP_MODE_GRAY,  /* minimap mode */
	"%s.tga",           /* minimap name format */
	MINIMAP_SIDECAR_NONE, /* minimap sidecar format */
	"RBSP",             /* bsp file prefix */
	1,                  /* bsp file version */
	qfalse,             /* cod-style lump len/ofs order */
	LoadRBSPFile,       /* bsp load function */
	WriteRBSPFile,      /* bsp write function */

	{
		/* name				contentFlags				contentFlagsClear			surfaceFlags				surfaceFlagsClear			compileFlags				compileFlagsClear */

		/* default */
		{ "default",        JA_CONT_SOLID,              -1,                         0,                          -1,                         C_SOLID,                    -1 },


		/* ydnar */
		{ "lightgrid",      0,                          0,                          0,                          0,                          C_LIGHTGRID,                0 },
		{ "antiportal",     0,                          0,                          0,                          0,                          C_ANTIPORTAL,               0 },
		{ "skip",           0,                          0,                          0,                          0,                          C_SKIP,                     0 },


		/* compiler */
		{ "origin",         JA_CONT_ORIGIN,             JA_CONT_SOLID,              0,                          0,                          C_ORIGIN | C_TRANSLUCENT,   C_SOLID },
		{ "areaportal",     JA_CONT_AREAPORTAL,         JA_CONT_SOLID,              0,                          0,                          C_AREAPORTAL | C_TRANSLUCENT,   C_SOLID },
		{ "trans",          JA_CONT_TRANSLUCENT,        0,                          0,                          0,                          C_TRANSLUCENT,              0 },
		{ "detail",         JA_CONT_DETAIL,             0,                          0,                          0,                          C_DETAIL,                   0 },
		{ "structural",     JA_CONT_STRUCTURAL,         0,                          0,                          0,                          C_STRUCTURAL,               0 },
		{ "hint",           0,                          0,                          JA_SURF_HINT,               0,                          C_HINT,                     0 },
		{ "nodraw",         0,                          0,                          JA_SURF_NODRAW,             0,                          C_NODRAW,                   0 },

		{ "alphashadow",    0,                          0,                          JA_SURF_ALPHASHADOW,        0,                          C_ALPHASHADOW | C_TRANSLUCENT,  0 },
		{ "lightfilter",    0,                          0,                          JA_SURF_LIGHTFILTER,        0,                          C_LIGHTFILTER | C_TRANSLUCENT,  0 },
		{ "nolightmap",     0,                          0,                          JA_SURF_NOLIGHTMAP,         0,                          C_VERTEXLIT,                0 },
		{ "pointlight",     0,                          0,                          JA_SURF_POINTLIGHT,         0,                          C_VERTEXLIT,                0 },


		/* game */
		{ "nonsolid",       0,                          JA_CONT_SOLID,              JA_SURF_NONSOLID,           0,                          0,                          C_SOLID },

		{ "trigger",        JA_CONT_TRIGGER,            JA_CONT_SOLID,              0,                          0,                          C_TRANSLUCENT,              C_SOLID },

		{ "water",          JA_CONT_WATER,              JA_CONT_SOLID,              0,                          0,                          C_LIQUID | C_TRANSLUCENT,   C_SOLID },
		{ "slime",          JA_CONT_SLIME,              JA_CONT_SOLID,              0,                          0,                          C_LIQUID | C_TRANSLUCENT,   C_SOLID },
		{ "lava",           JA_CONT_LAVA,               JA_CONT_SOLID,              0,                          0,                          C_LIQUID | C_TRANSLUCENT,   C_SOLID },

		{ "playerclip",     JA_CONT_PLAYERCLIP,         JA_CONT_SOLID,              0,                          0,                          C_DETAIL | C_TRANSLUCENT,   C_SOLID },
		{ "monsterclip",    JA_CONT_MONSTERCLIP,        JA_CONT_SOLID,              0,                          0,                          C_DETAIL | C_TRANSLUCENT,   C_SOLID },
		{ "nodrop",         JA_CONT_NODROP,             JA_CONT_SOLID,              0,                          0,                          C_TRANSLUCENT,              C_SOLID },

		{ "clusterportal",  JA_CONT_CLUSTERPORTAL,      JA_CONT_SOLID,              0,                          0,                          C_TRANSLUCENT,              C_SOLID },
		{ "donotenter",     JA_CONT_DONOTENTER,         JA_CONT_SOLID,              0,                          0,                          C_TRANSLUCENT,              C_SOLID },
		{ "botclip",        JA_CONT_BOTCLIP,            JA_CONT_SOLID,              0,                          0,                          C_TRANSLUCENT,              C_SOLID },

		{ "fog",            JA_CONT_FOG,                JA_CONT_SOLID,              0,                          0,                          C_FOG,                      C_SOLID },
		{ "sky",            0,                          0,                          JA_SURF_SKY,                0,                          C_SKY,                      0 },

		{ "slick",          0,                          0,                          JA_SURF_SLICK,              0,                          0,                          0 },

		{ "noimpact",       0,                          0,                          JA_SURF_NOIMPACT,           0,                          0,                          0 },
		{ "nomarks",        0,                          0,                          JA_SURF_NOMARKS,            0,                          C_NOMARKS,                  0 },
		{ "ladder",         0,                          0,                          JA_SURF_LADDER,             0,                          0,                          0 },
		{ "nodamage",       0,                          0,                          JA_SURF_NODAMAGE,           0,                          0,                          0 },
		{ "metalsteps",     0,                          0,                          JA_SURF_METALSTEPS,         0,                          0,                          0 },
		{ "flesh",          0,                          0,                          JA_SURF_FLESH,              0,                          0,                          0 },
		{ "nosteps",        0,                          0,                          JA_SURF_NOSTEPS,            0,                          0,                          0 },
		{ "nodlight",       0,                          0,                          JA_SURF_NODLIGHT,           0,                          0,                          0 },
		{ "forcefield",     0,                          0,                          JA_SURF_FORCEFIELD,         0,                          0,                          0 },

		/* null */
		{ NULL, 0, 0, 0, 0, 0, 0 }
	}
}



/* end marker */
#endif
