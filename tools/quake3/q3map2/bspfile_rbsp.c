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
#define BSPFILE_RBSP_C



/* dependencies */
#include "q3map2.h"




/* -------------------------------------------------------------------------------

   this file handles translating the bsp file format used by raven (jk2, ja, sof2)
   into the abstracted bsp file used by q3map2.

   ------------------------------------------------------------------------------- */

/* constants */
#define LUMP_ENTITIES       0
#define LUMP_SHADERS        1
#define LUMP_PLANES         2
#define LUMP_NODES          3
#define LUMP_LEAFS          4
#define LUMP_LEAFSURFACES   5
#define LUMP_LEAFBRUSHES    6
#define LUMP_MODELS         7
#define LUMP_BRUSHES        8
#define LUMP_BRUSHSIDES     9
#define LUMP_DRAWVERTS      10
#define LUMP_DRAWINDEXES    11
#define LUMP_FOGS           12
#define LUMP_SURFACES       13
#define LUMP_LIGHTMAPS      14
#define LUMP_LIGHTGRID      15
#define LUMP_VISIBILITY     16
#define LUMP_ADVERTISEMENTS 17 // Not sure if JKA uses this, but Q3M2 expects it for some versions
#define HEADER_LUMPS        31


/* types */
typedef struct
{
	char ident[ 4 ];
	int version;

	bspLump_t lumps[ HEADER_LUMPS ];
}
rbspHeader_t;



/* brush sides */
typedef struct
{
	int planeNum;
	int shaderNum;
	int surfaceNum;
}
rbspBrushSide_t;


static void CopyBrushSidesLump( rbspHeader_t *header ){
	int i;
	rbspBrushSide_t *in;
	bspBrushSide_t  *out;


	/* get count */
	numBSPBrushSides = GetLumpElements( (bspHeader_t*) header, LUMP_BRUSHSIDES, sizeof( *in ) );

	/* copy */
	in = GetLump( (bspHeader_t*) header, LUMP_BRUSHSIDES );
	for ( i = 0; i < numBSPBrushSides; i++ )
	{
		AUTOEXPAND_BY_REALLOC( bspBrushSides, i, allocatedBSPBrushSides, 1024 );
		out = &bspBrushSides[i];
		out->planeNum = in->planeNum;
		out->shaderNum = in->shaderNum;
		out->surfaceNum = in->surfaceNum;
		in++;
	}
}


/* drawsurfaces */
typedef struct rbspDrawSurface_s
{
	int shaderNum;
	int fogNum;
	int surfaceType;

	int firstVert;
	int numVerts;

	int firstIndex;
	int numIndexes;

	byte lightmapStyles[ MAX_LIGHTMAPS ];
	byte vertexStyles[ MAX_LIGHTMAPS ];
	int lightmapNum[ MAX_LIGHTMAPS ];
	int lightmapX[ MAX_LIGHTMAPS ], lightmapY[ MAX_LIGHTMAPS ];
	int lightmapWidth, lightmapHeight;

	vec3_t lightmapOrigin;
	vec3_t lightmapVecs[ 3 ];

	int patchWidth;
	int patchHeight;
}
rbspDrawSurface_t;


static void CopyDrawSurfacesLump( rbspHeader_t *header ){
	int i, j;
	rbspDrawSurface_t   *in;
	bspDrawSurface_t    *out;


	/* get count */
	numBSPDrawSurfaces = GetLumpElements( (bspHeader_t*) header, LUMP_SURFACES, sizeof( *in ) );
	SetDrawSurfaces( numBSPDrawSurfaces );

	/* copy */
	in = GetLump( (bspHeader_t*) header, LUMP_SURFACES );
	out = bspDrawSurfaces;
	for ( i = 0; i < numBSPDrawSurfaces; i++ )
	{
		out->shaderNum = in->shaderNum;
		out->fogNum = in->fogNum;
		out->surfaceType = in->surfaceType;
		out->firstVert = in->firstVert;
		out->numVerts = in->numVerts;
		out->firstIndex = in->firstIndex;
		out->numIndexes = in->numIndexes;

		for ( j = 0; j < MAX_LIGHTMAPS; j++ )
		{
			out->lightmapStyles[ j ] = in->lightmapStyles[ j ];
			out->vertexStyles[ j ] = in->vertexStyles[ j ];
			out->lightmapNum[ j ] = in->lightmapNum[ j ];
			out->lightmapX[ j ] = in->lightmapX[ j ];
			out->lightmapY[ j ] = in->lightmapY[ j ];
		}

		out->lightmapWidth = in->lightmapWidth;
		out->lightmapHeight = in->lightmapHeight;

		VectorCopy( in->lightmapOrigin, out->lightmapOrigin );
		VectorCopy( in->lightmapVecs[ 0 ], out->lightmapVecs[ 0 ] );
		VectorCopy( in->lightmapVecs[ 1 ], out->lightmapVecs[ 1 ] );
		VectorCopy( in->lightmapVecs[ 2 ], out->lightmapVecs[ 2 ] );

		out->patchWidth = in->patchWidth;
		out->patchHeight = in->patchHeight;

		in++;
		out++;
	}
}


/* drawverts */
typedef struct
{
	vec3_t xyz;
	float st[ 2 ];
	float lightmap[ MAX_LIGHTMAPS ][ 2 ];
	vec3_t normal;
	byte color[ MAX_LIGHTMAPS ][ 4 ];
}
rbspDrawVert_t;


static void CopyDrawVertsLump( rbspHeader_t *header ){
	int i, j;
	rbspDrawVert_t  *in;
	bspDrawVert_t   *out;


	/* get count */
	numBSPDrawVerts = GetLumpElements( (bspHeader_t*) header, LUMP_DRAWVERTS, sizeof( *in ) );
	SetDrawVerts( numBSPDrawVerts );

	/* copy */
	in = GetLump( (bspHeader_t*) header, LUMP_DRAWVERTS );
	out = bspDrawVerts;
	for ( i = 0; i < numBSPDrawVerts; i++ )
	{
		VectorCopy( in->xyz, out->xyz );
		out->st[ 0 ] = in->st[ 0 ];
		out->st[ 1 ] = in->st[ 1 ];

		for ( j = 0; j < MAX_LIGHTMAPS; j++ )
		{
			out->lightmap[ j ][ 0 ] = in->lightmap[ j ][ 0 ];
			out->lightmap[ j ][ 1 ] = in->lightmap[ j ][ 1 ];

			out->color[ j ][ 0 ] = in->color[ j ][ 0 ];
			out->color[ j ][ 1 ] = in->color[ j ][ 1 ];
			out->color[ j ][ 2 ] = in->color[ j ][ 2 ];
			out->color[ j ][ 3 ] = in->color[ j ][ 3 ];
		}

		VectorCopy( in->normal, out->normal );

		in++;
		out++;
	}
}


/* light grid */
typedef struct
{
	byte ambient[ MAX_LIGHTMAPS ][ 3 ];
	byte directed[ MAX_LIGHTMAPS ][ 3 ];
	byte styles[ MAX_LIGHTMAPS ];
	byte latLong[ 2 ];
}
rbspGridPoint_t;


static void CopyLightGridLumps( rbspHeader_t *header ){
	int i, j;
	rbspGridPoint_t *in;
	bspGridPoint_t  *out;


	/* get count */
	numBSPGridPoints = GetLumpElements( (bspHeader_t*) header, LUMP_LIGHTGRID, sizeof( *in ) );

	/* allocate buffer */
	bspGridPoints = safe_malloc0( numBSPGridPoints * sizeof( *bspGridPoints ) );

	/* copy */
	in = GetLump( (bspHeader_t*) header, LUMP_LIGHTGRID );
	out = bspGridPoints;
	for ( i = 0; i < numBSPGridPoints; i++ )
	{
		for ( j = 0; j < MAX_LIGHTMAPS; j++ )
		{
			VectorCopy( in->ambient[ j ], out->ambient[ j ] );
			VectorCopy( in->directed[ j ], out->directed[ j ] );
			out->styles[ j ] = in->styles[ j ];
		}

		out->latLong[ 0 ] = in->latLong[ 0 ];
		out->latLong[ 1 ] = in->latLong[ 1 ];

		in++;
		out++;
	}
}


/*
   LoadRBSPFile()
   loads a raven bsp file into memory
 */

void LoadRBSPFile( const char *filename ){
	rbspHeader_t    *header;

	/* load the file header */
	LoadFile( filename, (void**) &header );

	Sys_Printf("RBSP Lump Sizes:\n");
	Sys_Printf("  Shaders: %d (expected %d)\n", header->lumps[LUMP_SHADERS].length, (int)sizeof(bspShader_t));
	Sys_Printf("  Surfaces: %d (expected %d)\n", header->lumps[LUMP_SURFACES].length, 140); // 140 is approx for 2 lightmaps?
	Sys_Printf("  DrawVerts: %d (expected %d)\n", header->lumps[LUMP_DRAWVERTS].length, 64); // 64 for 2 lightmaps


	/* swap the header (except the first 4 bytes) */
	SwapBlock( (int*) ( (byte*) header + sizeof( int ) ), sizeof( *header ) - sizeof( int ) );

	/* make sure it matches the format we're trying to load */
	if ( force == qfalse && *( (int*) header->ident ) != *( (int*) game->bspIdent ) ) {
		Error( "%s is not a %s file", filename, game->bspIdent );
	}
	if ( force == qfalse && header->version != game->bspVersion ) {
		Error( "%s is version %d, not %d", filename, header->version, game->bspVersion );
	}

	/* load/convert lumps */
	numBSPShaders = CopyLump_Allocate( (bspHeader_t*) header, LUMP_SHADERS, (void **) &bspShaders, sizeof( bspShader_t ), &allocatedBSPShaders );

	numBSPModels = CopyLump_Allocate( (bspHeader_t*) header, LUMP_MODELS, (void **) &bspModels, sizeof( bspModel_t ), &allocatedBSPModels );

	numBSPPlanes = CopyLump_Allocate( (bspHeader_t*) header, LUMP_PLANES, (void **) &bspPlanes, sizeof( bspPlane_t ), &allocatedBSPPlanes );

	numBSPLeafs = CopyLump( (bspHeader_t*) header, LUMP_LEAFS, bspLeafs, sizeof( bspLeaf_t ) );

	numBSPNodes = CopyLump_Allocate( (bspHeader_t*) header, LUMP_NODES, (void **) &bspNodes, sizeof( bspNode_t ), &allocatedBSPNodes );

	numBSPLeafSurfaces = CopyLump_Allocate( (bspHeader_t*) header, LUMP_LEAFSURFACES, (void **) &bspLeafSurfaces, sizeof( bspLeafSurfaces[ 0 ] ), &allocatedBSPLeafSurfaces );

	numBSPLeafBrushes = CopyLump_Allocate( (bspHeader_t*) header, LUMP_LEAFBRUSHES, (void **) &bspLeafBrushes, sizeof( bspLeafBrushes[ 0 ] ), &allocatedBSPLeafBrushes );

	numBSPBrushes = CopyLump_Allocate( (bspHeader_t*) header, LUMP_BRUSHES, (void **) &bspBrushes, sizeof( bspBrush_t ), &allocatedBSPLeafBrushes );

	CopyBrushSidesLump( header );

	CopyDrawVertsLump( header );

	CopyDrawSurfacesLump( header );

	numBSPFogs = CopyLump( (bspHeader_t*) header, LUMP_FOGS, bspFogs, sizeof( bspFog_t ) );

	numBSPDrawIndexes = CopyLump_Allocate( (bspHeader_t*) header, LUMP_DRAWINDEXES, (void **) &bspDrawIndexes, sizeof( bspDrawIndexes[ 0 ] ), &allocatedBSPDrawIndexes );

	numBSPVisBytes = CopyLump( (bspHeader_t*) header, LUMP_VISIBILITY, bspVisBytes, 1 );

	numBSPLightBytes = GetLumpElements( (bspHeader_t*) header, LUMP_LIGHTMAPS, 1 );
	bspLightBytes = safe_malloc( numBSPLightBytes );
	CopyLump( (bspHeader_t*) header, LUMP_LIGHTMAPS, bspLightBytes, 1 );

	bspEntDataSize = CopyLump_Allocate( (bspHeader_t*) header, LUMP_ENTITIES, (void **) &bspEntData, 1, &allocatedBSPEntData );

	CopyLightGridLumps( header );

	/* advertisements */
	numBSPAds = 0;

	/* free the file buffer */
	free( header );
}



/*
   WriteRBSPFile()
   writes an rbsp bsp file
 */

void WriteRBSPFile( const char *filename ){
	Sys_Printf( "WriteRBSPFile is a stub" );
}
