/*
   ===========================================================================
   Copyright (C) 2012 Unvanquished Developers

   This file is part of Daemon source code.

   Daemon source code is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the License,
   or (at your option) any later version.

   Daemon source code is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Daemon source code; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
   ===========================================================================
 */
// nav.cpp -- navigation mesh generator interface

#include "q3map2.h"

static const char *Nav_ValueForKey( const entity_t *ent, const char *key ) {
	epair_t *ep;
	for ( ep = ent->epairs ; ep ; ep = ep->next ) {
		if ( !strcmp( ep->key, key ) ) {
			return ep->value;
		}
	}
	return "";
}

static qboolean Nav_GetVectorForKey( const entity_t *ent, const char *key, vec3_t vec ) {
	const char  *k = Nav_ValueForKey( ent, key );
	double v1 = 0, v2 = 0, v3 = 0;
	sscanf( k, "%lf %lf %lf", &v1, &v2, &v3 );
	vec[0] = v1;
	vec[1] = v2;
	vec[2] = v3;
	return k[0] != '\0' ? qtrue : qfalse;
}

static entity_t *Nav_FindTargetEntity( const char *target ) {
	for ( int i = 0; i < numEntities; i++ ) {
		const char *tname = Nav_ValueForKey( &entities[i], "targetname" );
		if ( !strcmp( target, tname ) ) {
			return &entities[i];
		}
	}
	return NULL;
}

#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
#include <set>
#include <ctime>
#include "cm_patch.h"
#include "navgen.h"
#include "DetourNavMeshQuery.h"

class UnvContext : public rcContext
{
	/// Clears all log entries.
	void doResetLog() override
	{
	}

	/// Logs a message.
	///  @param[in]   category  The category of the message.
	///  @param[in]   msg     The formatted message.
	///  @param[in]   len     The length of the formatted message.
	void doLog(const rcLogCategory /*category*/, const char* msg, const int /*len*/) override
	{
		if( m_logEnabled )
		{
			fprintf( stderr, "\n%s\n", msg );
		}
	}

	/// Clears all timers. (Resets all to unused.)
	void doResetTimers() override
	{
	}

	/// Starts the specified performance timer.
	///  @param[in]   label The category of timer.
	void doStartTimer(const rcTimerLabel /*label*/) override
	{
	}

	/// Stops the specified performance timer.
	///  @param[in]   label The category of the timer.
	void doStopTimer(const rcTimerLabel /*label*/) override
	{
	}

	/// Returns the total accumulated time of the specified performance timer.
	///  @param[in]   label The category of the timer.
	///  @return The accumulated time of the timer, or -1 if timers are disabled or the timer has never been started.
	int doGetAccumulatedTime(const rcTimerLabel /*label*/) const override
	{
		return -1;
	}
};

Geometry geo;

float cellHeight = 2.0f;
float stepSize = STEPSIZE;
int tileSize = 64;

struct Character
{
	const char *name;   //appended to filename
	float radius; //radius of agents (BBox maxs[0] or BBox maxs[1])
	float height; //height of agents (BBox maxs[2] - BBox mins[2])
};

static const Character characterArray[] = {
	{ "jka", 15.0f, 64.0f }
};

// flag for using meters (scale 0.0254 and rotate 90 0 0) — on by default for JA
static qboolean useMeters = qtrue;

// flag for solo mesh (single tile)
// Default ON for JKA: the solo path writes .nav files (JKA format) and runs
// off-mesh connection extraction. The tiled path writes .navMesh (Unvanquished).
static qboolean useSoloMesh = qtrue;

//flag for excluding caulk surfaces
static qboolean excludeCaulk = qtrue;

//flag for excluding surfaces with surfaceparm sky from navmesh generation
static qboolean excludeSky = qtrue;

//flag for adding new walkable spans so bots can walk over small gaps
static qboolean filterGaps = qtrue;

static void WriteNavMeshFile( const char* agentname, const dtTileCache *tileCache, const dtNavMeshParams *params ) {
	int numTiles = 0;
	FILE *file = NULL;
	char filename[ 1024 ];
	char filenameWithoutExt[ 1024 ];
	NavMeshSetHeader header;
	const int maxTiles = tileCache->getTileCount();

	for ( int i = 0; i < maxTiles; i++ )
	{
		const dtCompressedTile *tile = tileCache->getTile( i );
		if ( !tile || !tile->header || !tile->dataSize ) {
			continue;
		}
		numTiles++;
	}

	header.magic = NAVMESHSET_MAGIC;
	header.version = NAVMESHSET_VERSION;
	header.numTiles = numTiles;
	header.cacheParams = *tileCache->getParams();
	header.params = *params;

	SwapNavMeshSetHeader( header );

	strcpy( filenameWithoutExt, source );
	StripExtension( filenameWithoutExt );

	if ( snprintf( filename, sizeof( filename ), "%s-%s", filenameWithoutExt, agentname ) < 0 )
	{
		Error( "Filename too long for agent: %s\n", agentname );
	}

	DefaultExtension( filename, ".navMesh" );
	file = fopen( filename, "wb" );

	if ( !file ) {
		Error( "Error opening %s: %s\n", filename, strerror( errno ) );
	}

	fwrite( &header, sizeof( header ), 1, file );

	for ( int i = 0; i < maxTiles; i++ )
	{
		const dtCompressedTile *tile = tileCache->getTile( i );

		if ( !tile || !tile->header || !tile->dataSize ) {
			continue;
		}

		NavMeshTileHeader tileHeader;
		tileHeader.tileRef = tileCache->getTileRef( tile );
		tileHeader.dataSize = tile->dataSize;

		SwapNavMeshTileHeader( tileHeader );
		fwrite( &tileHeader, sizeof( tileHeader ), 1, file );

		unsigned char* data = ( unsigned char * ) malloc( tile->dataSize );

		memcpy( data, tile->data, tile->dataSize );
		if ( LittleLong( 1 ) != 1 ) {
			dtTileCacheHeaderSwapEndian( data, tile->dataSize );
		}

		fwrite( data, tile->dataSize, 1, file );

		free( data );
	}
	fclose( file );
}

//need this to get the windings for brushes
extern "C" qboolean FixWinding( winding_t* w );

static void AddVert( std::vector<float> &verts, std::vector<int> &tris, vec3_t vert ) {
	vec3_t transformedVert;
	VectorCopy( vert, transformedVert );

	if ( useMeters ) {
		// Scale to meters
		transformedVert[0] *= 0.0254f;
		transformedVert[1] *= 0.0254f;
		transformedVert[2] *= 0.0254f;
	}

	vec3_t recastVert;
	VectorCopy( transformedVert, recastVert );
	quake2recast( recastVert );
	int index = 0;
	for ( int i = 0; i < 3; i++ ) {
		verts.push_back( recastVert[i] );
	}
	index = ( verts.size() - 3 ) / 3;
	tris.push_back( index );
}

static void AddTri( std::vector<float> &verts, std::vector<int> &tris, vec3_t v1, vec3_t v2, vec3_t v3 ) {
	AddVert( verts, tris, v1 );
	AddVert( verts, tris, v2 );
	AddVert( verts, tris, v3 );
}

static void LoadBrushTris( std::vector<float> &verts, std::vector<int> &tris ) {
	int j;

	int solidFlags = 1 | 0x10000 | 0x20000 | 0x400000 | 0x8000000 | 0x20000000 | 0x80000000; // SOLID|PCLIP|MCLIP|BOTCLIP|DETAIL|TRANS|NODROP
	int temp = 0;
	int surfaceSkip = 0;

	char surfaceparm[16];

	strcpy( surfaceparm, "default" );
	ApplySurfaceParm( surfaceparm, &solidFlags, NULL, NULL );

	strcpy( surfaceparm, "playerclip" );
	ApplySurfaceParm( surfaceparm, &temp, NULL, NULL );
	solidFlags |= temp;

	if ( excludeSky ) {
		strcpy( surfaceparm, "sky" );
		ApplySurfaceParm( surfaceparm, NULL, &surfaceSkip, NULL );
	}

	Sys_Printf("Scanning geometry...\n");

	for ( int m = 0; m < numBSPModels; m++ ) {
		bspModel_t *model = &bspModels[m];

		//go through the brushes
		for ( int i = model->firstBSPBrush, count = 0; count < model->numBSPBrushes; i++, count++ ) {
			int numSides = bspBrushes[i].numSides;
			int firstSide = bspBrushes[i].firstSide;
			bspShader_t *brushShader = &bspShaders[bspBrushes[i].shaderNum];

						if ( !( brushShader->contentFlags & solidFlags ) ) {
				if (m > 0 && count < 5) Sys_Printf("  Model %d Skipping brush %d: CF=0x%x Shader=%s\n", m, i, brushShader->contentFlags, brushShader->shader);
				continue;
			}
			/* walk the list of brush sides */
			for ( int p = 0; p < numSides; p++ )
			{
				/* get side and plane */
				bspBrushSide_t *side = &bspBrushSides[p + firstSide];
				bspPlane_t *plane = &bspPlanes[side->planeNum];
				bspShader_t *shader = &bspShaders[side->shaderNum];

				if ( shader->surfaceFlags & surfaceSkip ) {
					continue;
				}

				if ( excludeCaulk && !Q_stricmp( shader->shader, "textures/common/caulk" ) ) {
					continue;
				}

				/* make huge winding */
				winding_t *w = BaseWindingForPlane( plane->normal, plane->dist );

				/* walk the list of brush sides */
				for ( j = 0; j < numSides && w != NULL; j++ )
				{
					bspBrushSide_t *chopSide = &bspBrushSides[j + firstSide];
					if ( chopSide == side ) {
						continue;
					}
					if ( chopSide->planeNum == ( side->planeNum ^ 1 ) ) {
						continue;       /* back side clipaway */

					}
					bspPlane_t *chopPlane = &bspPlanes[chopSide->planeNum ^ 1];

					ChopWindingInPlace( &w, chopPlane->normal, chopPlane->dist, 0 );

					/* ydnar: fix broken windings that would generate trifans */
					FixWinding( w );
				}

				if ( w ) {
					for ( int j = 2; j < w->numpoints; j++ ) {
						AddTri( verts, tris, w->p[0], w->p[j - 1], w->p[j] );
					}
					FreeWinding( w );
				}
			}
		}
	}
}

static qboolean BoundsIntersect( const vec3_t mins, const vec3_t maxs, const vec3_t mins2, const vec3_t maxs2 ){
	if ( maxs[ 0 ] < mins2[ 0 ] ||
		 maxs[ 1 ] < mins2[ 1 ] || maxs[ 2 ] < mins2[ 2 ] || mins[ 0 ] > maxs2[ 0 ] || mins[ 1 ] > maxs2[ 1 ] || mins[ 2 ] > maxs2[ 2 ] ) {
		return qfalse;
	}

	return qtrue;
}

static void LoadPatchTris( std::vector<float> &verts, std::vector<int> &tris ) {
	int solidFlags = 1 | 0x10000 | 0x20000 | 0x400000 | 0x8000000 | 0x20000000 | 0x80000000; // SOLID|PCLIP|MCLIP|BOTCLIP|DETAIL|TRANS|NODROP
	int processed = 0, addedTris = 0;

	for ( int m = 0; m < numBSPModels; m++ ) {
		const bspModel_t *model = &bspModels[m];
		for ( int k = model->firstBSPSurface, n = 0; n < model->numBSPSurfaces; k++, n++ )
		{
			const bspDrawSurface_t *surface = &bspDrawSurfaces[k];

			if ( surface->surfaceType == MST_PATCH ) {
				if ( !surface->patchWidth ) continue;

				cGrid_t grid;
				grid.width = surface->patchWidth;
				grid.height = surface->patchHeight;
				grid.wrapHeight = qfalse;
				grid.wrapWidth = qfalse;

				bspDrawVert_t *curveVerts = &bspDrawVerts[surface->firstVert];
				for ( int x = 0; x < grid.width; x++ ) {
					for ( int y = 0; y < grid.height; y++ ) {
						VectorCopy( curveVerts[ y * grid.width + x ].xyz, grid.points[ x ][ y ] );
					}
				}

				CM_SetGridWrapWidth( &grid );
				CM_SubdivideGridColumns( &grid );
				CM_RemoveDegenerateColumns( &grid );
				CM_TransposeGrid( &grid );
				CM_SetGridWrapWidth( &grid );
				CM_SubdivideGridColumns( &grid );
				CM_RemoveDegenerateColumns( &grid );

				for ( int x = 0; x < ( grid.width - 1 ); x++ ) {
					for ( int y = 0; y < ( grid.height - 1 ); y++ ) {
						AddTri( verts, tris, grid.points[ x ][ y ], grid.points[ x + 1 ][ y ], grid.points[ x + 1 ][ y + 1 ] );
						AddTri( verts, tris, grid.points[ x + 1 ][ y + 1 ], grid.points[ x + 1 ][ y ], grid.points[ x ][ y ] ); // backface
						AddTri( verts, tris, grid.points[ x + 1 ][ y + 1 ], grid.points[ x ][ y + 1 ], grid.points[ x ][ y ] );
						AddTri( verts, tris, grid.points[ x ][ y ], grid.points[ x ][ y + 1 ], grid.points[ x + 1 ][ y + 1 ] ); // backface
						addedTris += 4;
					}
				}
				processed++;
			} 
			else if ( surface->surfaceType == MST_PLANAR || surface->surfaceType == MST_TRIANGLE_SOUP ) {
				for ( int i = 0; i < surface->numIndexes; i += 3 ) {
					int i1 = bspDrawIndexes[ surface->firstIndex + i ];
					int i2 = bspDrawIndexes[ surface->firstIndex + i + 1 ];
					int i3 = bspDrawIndexes[ surface->firstIndex + i + 2 ];

					AddTri( verts, tris, bspDrawVerts[ surface->firstVert + i1 ].xyz, bspDrawVerts[ surface->firstVert + i2 ].xyz, bspDrawVerts[ surface->firstVert + i3 ].xyz );
					AddTri( verts, tris, bspDrawVerts[ surface->firstVert + i3 ].xyz, bspDrawVerts[ surface->firstVert + i2 ].xyz, bspDrawVerts[ surface->firstVert + i1 ].xyz ); // backface
					addedTris += 2;
				}
				processed++;
			}
		}
	}
	Sys_Printf("Surface extraction complete: %d surfaces processed, %d triangles added.\n", processed, addedTris);
}
static void LoadGeometry(){
	std::vector<float> verts;
	std::vector<int> tris;

	Sys_Printf( "loading geometry...\n" );
	int numVerts, numTris;

	//count surfaces
	LoadBrushTris( verts, tris );
	LoadPatchTris( verts, tris );

	numTris = tris.size() / 3;
	numVerts = verts.size() / 3;

	Sys_Printf( "Using %d triangles\n", numTris );
	Sys_Printf( "Using %d vertices\n", numVerts );

	geo.init( &verts[ 0 ], numVerts, &tris[ 0 ], numTris );

	const float *mins = geo.getMins();
	const float *maxs = geo.getMaxs();

	Sys_Printf( "set recast world bounds to\n" );
	Sys_Printf( "min: %f %f %f\n", mins[0], mins[1], mins[2] );
	Sys_Printf( "max: %f %f %f\n", maxs[0], maxs[1], maxs[2] );
}

// Modified version of Recast's rcErodeWalkableArea that uses an AABB instead of a cylindrical radius
static bool rcErodeWalkableAreaByBox( rcContext* ctx, int boxRadius, rcCompactHeightfield& chf ){
	rcAssert( ctx );

	const int w = chf.width;
	const int h = chf.height;

	ctx->startTimer( RC_TIMER_ERODE_AREA );

	unsigned char* dist = (unsigned char*)rcAlloc( sizeof( unsigned char ) * chf.spanCount, RC_ALLOC_TEMP );
	if ( !dist ) {
		ctx->log( RC_LOG_ERROR, "erodeWalkableArea: Out of memory 'dist' (%d).", chf.spanCount );
		return false;
	}

	// Init distance.
	memset( dist, 0xff, sizeof( unsigned char ) * chf.spanCount );

	// Mark boundary cells.
	for ( int y = 0; y < h; ++y )
	{
		for ( int x = 0; x < w; ++x )
		{
			const rcCompactCell& c = chf.cells[x + y * w];
			for ( int i = (int)c.index, ni = (int)( c.index + c.count ); i < ni; ++i )
			{
				if ( chf.areas[i] == RC_NULL_AREA ) {
					dist[i] = 0;
				}
				else
				{
					const rcCompactSpan& s = chf.spans[i];
					int nc = 0;
					for ( int dir = 0; dir < 4; ++dir )
					{
						if ( rcGetCon( s, dir ) != RC_NOT_CONNECTED ) {
							const int nx = x + rcGetDirOffsetX( dir );
							const int ny = y + rcGetDirOffsetY( dir );
							const int nidx = (int)chf.cells[nx + ny * w].index + rcGetCon( s, dir );
							if ( chf.areas[nidx] != RC_NULL_AREA ) {
								nc++;
							}
						}
					}
					// At least one missing neighbour.
					if ( nc != 4 ) {
						dist[i] = 0;
					}
				}
			}
		}
	}

	unsigned char nd;

	// Pass 1
	for ( int y = 0; y < h; ++y )
	{
		for ( int x = 0; x < w; ++x )
		{
			const rcCompactCell& c = chf.cells[x + y * w];
			for ( int i = (int)c.index, ni = (int)( c.index + c.count ); i < ni; ++i )
			{
				const rcCompactSpan& s = chf.spans[i];

				if ( rcGetCon( s, 0 ) != RC_NOT_CONNECTED ) {
					// (-1,0)
					const int ax = x + rcGetDirOffsetX( 0 );
					const int ay = y + rcGetDirOffsetY( 0 );
					const int ai = (int)chf.cells[ax + ay * w].index + rcGetCon( s, 0 );
					const rcCompactSpan& as = chf.spans[ai];
					nd = (unsigned char)rcMin( (int)dist[ai] + 2, 255 );
					if ( nd < dist[i] ) {
						dist[i] = nd;
					}

					// (-1,-1)
					if ( rcGetCon( as, 3 ) != RC_NOT_CONNECTED ) {
						const int aax = ax + rcGetDirOffsetX( 3 );
						const int aay = ay + rcGetDirOffsetY( 3 );
						const int aai = (int)chf.cells[aax + aay * w].index + rcGetCon( as, 3 );
						nd = (unsigned char)rcMin( (int)dist[aai] + 2, 255 );
						if ( nd < dist[i] ) {
							dist[i] = nd;
						}
					}
				}
				if ( rcGetCon( s, 3 ) != RC_NOT_CONNECTED ) {
					// (0,-1)
					const int ax = x + rcGetDirOffsetX( 3 );
					const int ay = y + rcGetDirOffsetY( 3 );
					const int ai = (int)chf.cells[ax + ay * w].index + rcGetCon( s, 3 );
					const rcCompactSpan& as = chf.spans[ai];
					nd = (unsigned char)rcMin( (int)dist[ai] + 2, 255 );
					if ( nd < dist[i] ) {
						dist[i] = nd;
					}

					// (1,-1)
					if ( rcGetCon( as, 2 ) != RC_NOT_CONNECTED ) {
						const int aax = ax + rcGetDirOffsetX( 2 );
						const int aay = ay + rcGetDirOffsetY( 2 );
						const int aai = (int)chf.cells[aax + aay * w].index + rcGetCon( as, 2 );
						nd = (unsigned char)rcMin( (int)dist[aai] + 2, 255 );
						if ( nd < dist[i] ) {
							dist[i] = nd;
						}
					}
				}
			}
		}
	}

	// Pass 2
	for ( int y = h - 1; y >= 0; --y )
	{
		for ( int x = w - 1; x >= 0; --x )
		{
			const rcCompactCell& c = chf.cells[x + y * w];
			for ( int i = (int)c.index, ni = (int)( c.index + c.count ); i < ni; ++i )
			{
				const rcCompactSpan& s = chf.spans[i];

				if ( rcGetCon( s, 2 ) != RC_NOT_CONNECTED ) {
					// (1,0)
					const int ax = x + rcGetDirOffsetX( 2 );
					const int ay = y + rcGetDirOffsetY( 2 );
					const int ai = (int)chf.cells[ax + ay * w].index + rcGetCon( s, 2 );
					const rcCompactSpan& as = chf.spans[ai];
					nd = (unsigned char)rcMin( (int)dist[ai] + 2, 255 );
					if ( nd < dist[i] ) {
						dist[i] = nd;
					}

					// (1,1)
					if ( rcGetCon( as, 1 ) != RC_NOT_CONNECTED ) {
						const int aax = ax + rcGetDirOffsetX( 1 );
						const int aay = ay + rcGetDirOffsetY( 1 );
						const int aai = (int)chf.cells[aax + aay * w].index + rcGetCon( as, 1 );
						nd = (unsigned char)rcMin( (int)dist[aai] + 2, 255 );
						if ( nd < dist[i] ) {
							dist[i] = nd;
						}
					}
				}
				if ( rcGetCon( s, 1 ) != RC_NOT_CONNECTED ) {
					// (0,1)
					const int ax = x + rcGetDirOffsetX( 1 );
					const int ay = y + rcGetDirOffsetY( 1 );
					const int ai = (int)chf.cells[ax + ay * w].index + rcGetCon( s, 1 );
					const rcCompactSpan& as = chf.spans[ai];
					nd = (unsigned char)rcMin( (int)dist[ai] + 2, 255 );
					if ( nd < dist[i] ) {
						dist[i] = nd;
					}

					// (-1,1)
					if ( rcGetCon( as, 0 ) != RC_NOT_CONNECTED ) {
						const int aax = ax + rcGetDirOffsetX( 0 );
						const int aay = ay + rcGetDirOffsetY( 0 );
						const int aai = (int)chf.cells[aax + aay * w].index + rcGetCon( as, 0 );
						nd = (unsigned char)rcMin( (int)dist[aai] + 2, 255 );
						if ( nd < dist[i] ) {
							dist[i] = nd;
						}
					}
				}
			}
		}
	}

	const unsigned char thr = (unsigned char)( boxRadius * 2 );
	for ( int i = 0; i < chf.spanCount; ++i ) {
		if ( dist[i] < thr ) {
			chf.areas[i] = RC_NULL_AREA;
		}
	}

	rcFree( dist );

	ctx->stopTimer( RC_TIMER_ERODE_AREA );

	return true;
}

/*
   rcFilterGaps

   Does a super simple sampling of ledges to detect and fix
   any "gaps" in the heightfield that are narrow enough for us to walk over
   because of our AABB based collision system
 */
static void rcFilterGaps( rcContext *ctx, int walkableRadius, int walkableClimb, int walkableHeight, rcHeightfield &solid ) {
	const int h = solid.height;
	const int w = solid.width;
	const int MAX_HEIGHT = 0xffff;
	std::vector<int> spanData;
	spanData.reserve( 500 * 3 );
	std::vector<int> data;
	data.reserve( ( walkableRadius * 2 - 1 ) * 3 );

	//for every span in the heightfield
	for ( int y = 0; y < h; ++y ) {
		for ( int x = 0; x < w; ++x ) {
			//check each span in the column
			for ( rcSpan *s = solid.spans[x + y * w]; s; s = s->next ) {
				//bottom and top of the "base" span
				const int sbot = s->smax;
				const int stop = ( s->next ) ? ( s->next->smin ) : MAX_HEIGHT;

				//if the span is walkable
				if ( s->area != RC_NULL_AREA ) {
					//check all neighbor connections
					for ( int dir = 0; dir < 4; dir++ ) {
						const int dirx = rcGetDirOffsetX( dir );
						const int diry = rcGetDirOffsetY( dir );
						int dx = x;
						int dy = y;
						bool freeSpace = false;
						bool stop = false;

						if ( dx < 0 || dy < 0 || dx >= w || dy >= h ) {
							continue;
						}

						//keep going the direction for walkableRadius * 2 - 1 spans
						//because we can walk as long as at least part of our bbox is on a solid surface
						for ( int i = 1; i < walkableRadius * 2; i++ ) {
							dx = dx + dirx;
							dy = dy + diry;
							if ( dx < 0 || dy < 0 || dx >= w || dy >= h ) {
								i--;
								freeSpace = false;
								stop = false;
								break;
							}

							//tells if there is space here for us to stand
							freeSpace = false;

							//go through the column
							for ( rcSpan *ns = solid.spans[dx + dy * w]; ns; ns = ns->next ) {
								int nsbot = ns->smax;
								int nstop = ( ns->next ) ? ( ns->next->smin ) : MAX_HEIGHT;

								//if there is a span within walkableClimb of the base span, we have reached the end of the gap (if any)
								if ( rcAbs( sbot - nsbot ) <= walkableClimb && ns->area != RC_NULL_AREA ) {
									//set flag telling us to stop
									stop = true;

									//only add spans if we have gone for more than 1 iteration
									//if we stop at the first iteration, it means there was no gap to begin with
									if ( i > 1 ) {
										freeSpace = true;
									}
									break;
								}

								if ( nsbot < sbot && nstop >= sbot + walkableHeight ) {
									//tell that we found walkable space within reach of the previous span
									freeSpace = true;
									//add this span to a temporary storage location
									data.push_back( dx );
									data.push_back( dy );
									data.push_back( sbot );
									break;
								}
							}

							//stop if there is no more freespace, or we have reached end of gap
							if ( stop || !freeSpace ) {
								break;
							}
						}
						//move the spans from the temp location to the storage
						//we check freeSpace to make sure we don't add a
						//span when we stop at the first iteration (because there is no gap)
						//checking stop tells us if there was a span to complete the "bridge"
						if ( freeSpace && stop ) {
							const int N = data.size();
							for ( int i = 0; i < N; i++ )
								spanData.push_back( data[i] );
						}
						data.clear();
					}
				}
			}
		}
	}

	//add the new spans
	//we cant do this in the loop, because the loop would then iterate over those added spans
	for ( std::vector<int>::size_type i = 0; i < spanData.size(); i += 3 ) {
		rcAddSpan( ctx, solid, spanData[i], spanData[i + 1], spanData[i + 2] - 1, spanData[i + 2], RC_WALKABLE_AREA, walkableClimb );
	}
}

static int rasterizeTileLayers( rcContext &context, int tx, int ty, const rcConfig &mcfg, TileCacheData *data, int maxLayers ){
	rcConfig cfg;

	FastLZCompressor comp;
	RasterizationContext rc;

	const float tcs = mcfg.tileSize * mcfg.cs;

	memcpy( &cfg, &mcfg, sizeof( cfg ) );

	// find tile bounds
	// easy optimisation here: avoid recalculating things Y * X times
	cfg.bmin[ 0 ] = mcfg.bmin[ 0 ] + tx * tcs;
	cfg.bmin[ 1 ] = mcfg.bmin[ 1 ];
	cfg.bmin[ 2 ] = mcfg.bmin[ 2 ] + ty * tcs;

	cfg.bmax[ 0 ] = mcfg.bmin[ 0 ] + ( tx + 1 ) * tcs;
	cfg.bmax[ 1 ] = mcfg.bmax[ 1 ];
	cfg.bmax[ 2 ] = mcfg.bmin[ 2 ] + ( ty + 1 ) * tcs;

	// expand bounds by border size
	// easy optimisation here: borderSize and cs (cs: The xz-plane cell size to use for fields)
	// are constants (coming directly from mcfg, previously named cfg, you follow?).
	cfg.bmin[ 0 ] -= cfg.borderSize * cfg.cs;
	cfg.bmin[ 2 ] -= cfg.borderSize * cfg.cs;

	cfg.bmax[ 0 ] += cfg.borderSize * cfg.cs;
	cfg.bmax[ 2 ] += cfg.borderSize * cfg.cs;

	rc.solid = rcAllocHeightfield();

	if ( !rcCreateHeightfield( &context, *rc.solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch ) ) {
		Error( "Failed to create heightfield for navigation mesh.\n" );
	}

	//I understand that using std::vector prevents NiH's proudness.
	const float *verts = geo.getVerts();
	const int nverts = geo.getNumVerts();
	const rcChunkyTriMesh *chunkyMesh = geo.getChunkyMesh();

	rc.triareas = new unsigned char[ chunkyMesh->maxTrisPerChunk ];
	//you know what? This will never be reached, because new throws exceptions, by default.
	//really, should just use STL or C, not raw C++ with C's bugs.
	if ( !rc.triareas ) {
		Error( "Out of memory rc.triareas\n" );
	}

	float tbmin[ 2 ], tbmax[ 2 ];

	tbmin[ 0 ] = cfg.bmin[ 0 ];
	tbmin[ 1 ] = cfg.bmin[ 2 ];
	tbmax[ 0 ] = cfg.bmax[ 0 ];
	tbmax[ 1 ] = cfg.bmax[ 2 ];

	int *cid = new int[ chunkyMesh->nnodes ];

	//do not search for this in libraries, you won't find it. It's in RecastDemo's code.
	//It " Creates partitioned triangle mesh (AABB tree), where each node contains at max trisPerChunk triangles."
	//why? No idea.
	const int ncid = rcGetChunksOverlappingRect( chunkyMesh, tbmin, tbmax, cid, chunkyMesh->nnodes );
	if ( !ncid ) {
		delete[] cid;
		return 0;
	}

	for ( int i = 0; i < ncid; i++ )
	{
		const rcChunkyTriMeshNode &node = chunkyMesh->nodes[ cid[ i ] ];
		const int *tris = &chunkyMesh->tris[ node.i * 3 ];
		const int ntris = node.n;

		memset( rc.triareas, 0, ntris * sizeof( unsigned char ) );

		rcMarkWalkableTriangles( &context, cfg.walkableSlopeAngle, verts, nverts, tris, ntris, rc.triareas );
		rcRasterizeTriangles( &context, verts, nverts, tris, rc.triareas, ntris, *rc.solid, cfg.walkableClimb );
	}

	delete[] cid;

	//makes them walkable (unlike the other filters, would probably kill some people to write meaningful code)
	rcFilterLowHangingWalkableObstacles( &context, cfg.walkableClimb, *rc.solid );

	//dont filter ledge spans since characters CAN walk on ledges due to using a bbox for movement collision
	//makes them un-walkable
	//rcFilterLedgeSpans (&context, cfg.walkableHeight, cfg.walkableClimb, *rc.solid);

	//makes them un-walkable
	rcFilterWalkableLowHeightSpans( &context, cfg.walkableHeight, *rc.solid );

	//by default, make gaps walkable (because using a BBox system makes agents cubes). In theory a great idea, but
	//it does not work correctly.
	if ( filterGaps ) {
		rcFilterGaps( &context, cfg.walkableRadius, cfg.walkableClimb, cfg.walkableHeight, *rc.solid );
	}

	rc.chf = rcAllocCompactHeightfield();

	if ( !rcBuildCompactHeightfield( &context, cfg.walkableHeight, cfg.walkableClimb, *rc.solid, *rc.chf ) ) {
		Error( "Failed to create compact heightfield for navigation mesh.\n" );
	}

	if ( !rcErodeWalkableAreaByBox( &context, cfg.walkableRadius, *rc.chf ) ) {
		Error( "Unable to erode walkable surfaces.\n" );
	}

	rc.lset = rcAllocHeightfieldLayerSet();

	if ( !rc.lset ) {
		Error( "Out of memory heightfield layer set\n" );
	}

	if ( !rcBuildHeightfieldLayers( &context, *rc.chf, cfg.borderSize, cfg.walkableHeight, *rc.lset ) ) {
		Error( "Could not build heightfield layers\n" );
	}

	rc.ntiles = 0;

	for ( int i = 0; i < rcMin( rc.lset->nlayers, MAX_LAYERS ); i++ )
	{
		TileCacheData *tile = &rc.tiles[ rc.ntiles++ ];
		const rcHeightfieldLayer *layer = &rc.lset->layers[ i ];

		dtTileCacheLayerHeader header;
		header.magic = DT_TILECACHE_MAGIC;
		header.version = DT_TILECACHE_VERSION;

		header.tx = tx;
		header.ty = ty;
		header.tlayer = i;
		dtVcopy( header.bmin, layer->bmin );
		dtVcopy( header.bmax, layer->bmax );

		header.width = ( unsigned char ) layer->width;
		header.height = ( unsigned char ) layer->height;
		header.minx = ( unsigned char ) layer->minx;
		header.maxx = ( unsigned char ) layer->maxx;
		header.miny = ( unsigned char ) layer->miny;
		header.maxy = ( unsigned char ) layer->maxy;
		header.hmin = ( unsigned short ) layer->hmin;
		header.hmax = ( unsigned short ) layer->hmax;

		dtStatus status = dtBuildTileCacheLayer( &comp, &header, layer->heights, layer->areas, layer->cons, &tile->data, &tile->dataSize );

		if ( dtStatusFailed( status ) ) {
			return 0;
		}
	}

	// transfer tile data over to caller
	int n = 0;
	for ( int i = 0; i < rcMin( rc.ntiles, maxLayers ); i++ )
	{
		data[ n++ ] = rc.tiles[ i ];
		rc.tiles[ i ].data = 0;
		rc.tiles[ i ].dataSize = 0;
	}

	return n;
}

static void WriteSoloNavMeshFile( const unsigned char* data, const int dataSize, const rcConfig& cfg ) {
	FILE *file = NULL;
	char filename[ 1024 ];
	char filenameWithoutExt[ 1024 ];

	strcpy( filenameWithoutExt, source );
	StripExtension( filenameWithoutExt );

	// Output extension: ".navmesh" matches Bot2's runtime loader
	// (codemp/game/g_navmesh.cpp opens "maps/<mapname>.navmesh"). The MSET
	// container itself is unchanged; only the filename extension differs.
	if ( snprintf( filename, sizeof( filename ), "%s.navmesh", filenameWithoutExt ) < 0 )
	{
		Error( "Filename too long for map: %s\n", source );
	}

	file = fopen( filename, "wb" );
	if ( !file ) {
		Error( "Error opening %s: %s\n", filename, strerror( errno ) );
	}

	Sys_Printf( "Writing %s (Solo-as-MSET Binary)\n", filename );

	struct {
		int magic;
		int version;
		int numTiles;
		dtNavMeshParams params;
	} header;

	header.magic = NAVMESHSET_MAGIC;
	header.version = 1;
	header.numTiles = 1;

	dtVcopy( header.params.orig, cfg.bmin );
	header.params.tileWidth = ( cfg.bmax[0] - cfg.bmin[0] );
	header.params.tileHeight = ( cfg.bmax[2] - cfg.bmin[2] );
	header.params.maxTiles = 1;
	header.params.maxPolys = 32768;

	fwrite( &header, sizeof( header ), 1, file );

	struct {
		dtTileRef tileRef;
		int dataSize;
	} tileHeader;

	tileHeader.tileRef = 1;
	tileHeader.dataSize = dataSize;

	fwrite( &tileHeader, sizeof( tileHeader ), 1, file );
	fwrite( data, dataSize, 1, file );

	fclose( file );
}

#define POLYAREA_ELEVATOR       10
#define POLYAREA_JUMPPAD        11

// Auto-detected movement connections (Pass 1-3)
#define AREA_JUMP_DROP          2   // walk off ledge, no jump needed
#define AREA_JUMP_BASIC         3   // running jump across gap
#define AREA_WALLRUN_ASCEND     4   // wallrun up vertical surface

static std::vector<float> offMeshConVerts;
static std::vector<float> offMeshConRad;
static std::vector<unsigned char> offMeshConDir;
static std::vector<unsigned char> offMeshConAreas;
static std::vector<unsigned short> offMeshConFlags;
static std::vector<unsigned int> offMeshConUserID;

// Bounding boxes of all trigger_hurt volumes, in Recast Y-up space.
// Used by Pass 1 to reject landing points inside damage zones.
struct HurtVolume {
	float mins[3]; // Recast space
	float maxs[3];
};
static std::vector<HurtVolume> hurtVolumes;

// Bounding boxes of all elevator (func_plat / func_door elevator) travel columns,
// in Quake space (Z-up, Quake units).  Stored as the full vertical travel range so
// that any point inside is potentially crushable.  Populated during entity processing
// and used by the sidecar loader to nudge start points clear of elevator paths.
struct ElevatorVolume {
	float minx, maxx; // Quake X
	float miny, maxy; // Quake Y
	float minz, maxz; // Quake Z  (full travel column, bottom to top)
	const char *modelStr; // for logging
};
static std::vector<ElevatorVolume> elevatorVolumes;

static void TransformPointToRecast(vec3_t pt) {
	if ( useMeters ) {
		pt[0] *= 0.0254f;
		pt[1] *= 0.0254f;
		pt[2] *= 0.0254f;
	}
	quake2recast(pt);
}

// Inverse of TransformPointToRecast -- converts a Recast-space point back to
// Quake game-space for logging.  (recast2quake is the same Y/Z swap.)
static void TransformPointToGame(const float *rcPt, vec3_t out) {
	out[0] = rcPt[0];
	out[1] = rcPt[1];
	out[2] = rcPt[2];
	recast2quake(out);
	if ( useMeters ) {
		const float inv = 1.0f / 0.0254f;
		out[0] *= inv;
		out[1] *= inv;
		out[2] *= inv;
	}
}

// -----------------------------------------------------------------------
// Shared infrastructure for auto-detected off-mesh connections (Pass 1-3)
// All coordinates are in Recast space (Y-up). Physics constants are scaled
// at call sites using the local `scale` variable (0.0254 if useMeters, 1.0 otherwise).
// -----------------------------------------------------------------------

struct HitResult {
	bool  hit;
	float point[3];
	float normal[3]; // always flipped to point upward (positive Y)
};

// Ray trace against the navmesh input geometry (derived from BSP solid brushes).
// Returns the closest hit along the segment [start, end].
static HitResult Nav_TraceRayHit( const float* start, const float* end ) {
	HitResult result;
	result.hit = false;
	result.point[0] = result.point[1] = result.point[2] = 0.0f;
	result.normal[0] = result.normal[1] = result.normal[2] = 0.0f;

	vec3_t dir;
	VectorSubtract( end, start, dir );
	float dist = VectorLength( dir );
	if ( dist < 0.01f ) return result;
	VectorScale( dir, 1.0f / dist, dir );

	ray_t ray;
	ray_construct_for_vec3( &ray, start, dir );

	const rcChunkyTriMesh* chunky = geo.getChunkyMesh();
	const float* verts            = geo.getVerts();

	// For a vertical ray, XZ start == XZ end; add a tiny epsilon so the
	// 2D segment query finds the right chunk.
	float p[2] = { start[0], start[2] };
	float q[2] = { end[0] + 0.001f,   end[2] + 0.001f };

	int ids[512];
	int nids = rcGetChunksOverlappingSegment( chunky, p, q, ids, 512 );

	float bestT = dist;

	for ( int i = 0; i < nids; ++i ) {
		const rcChunkyTriMeshNode& node = chunky->nodes[ids[i]];
		const int* tris = &chunky->tris[node.i * 3];
		for ( int j = 0; j < node.n; ++j ) {
			const float* v0 = &verts[tris[j * 3 + 0] * 3];
			const float* v1 = &verts[tris[j * 3 + 1] * 3];
			const float* v2 = &verts[tris[j * 3 + 2] * 3];

			float t = ray_intersect_triangle( &ray, qfalse, v0, v1, v2 );
			if ( t > 0.01f && t < bestT ) {
				bestT = t;
				result.hit      = true;
				result.point[0] = start[0] + dir[0] * t;
				result.point[1] = start[1] + dir[1] * t;
				result.point[2] = start[2] + dir[2] * t;

				// Triangle normal via cross product
				float e1[3] = { v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2] };
				float e2[3] = { v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2] };
				float nx = e1[1]*e2[2] - e1[2]*e2[1];
				float ny = e1[2]*e2[0] - e1[0]*e2[2];
				float nz = e1[0]*e2[1] - e1[1]*e2[0];
				float nl = sqrtf( nx*nx + ny*ny + nz*nz );
				if ( nl > 0.001f ) { nx /= nl; ny /= nl; nz /= nl; }
				// Always face upward
				if ( ny < 0.0f ) { nx = -nx; ny = -ny; nz = -nz; }
				result.normal[0] = nx;
				result.normal[1] = ny;
				result.normal[2] = nz;
			}
		}
	}
	return result;
}

// Boundary edge: midpoint + outward normal (XZ plane, Recast Y-up space).
struct BoundaryEdge {
	float mid[3];
	float normal[3]; // unit vector, Y == 0
};

// Iterate dtNavMesh polygons and collect all edges with no neighbour.
static std::vector<BoundaryEdge> Nav_GetBoundaryEdges( const dtNavMesh* mesh ) {
	std::vector<BoundaryEdge> edges;

	for ( int ti = 0; ti < mesh->getMaxTiles(); ++ti ) {
		const dtMeshTile* tile = mesh->getTile( ti );
		if ( !tile || !tile->header ) continue;

		for ( int pi = 0; pi < tile->header->polyCount; ++pi ) {
			const dtPoly* poly = &tile->polys[pi];
			if ( poly->getType() != DT_POLYTYPE_GROUND ) continue;

			for ( int ei = 0; ei < poly->vertCount; ++ei ) {
				if ( poly->neis[ei] != 0 ) continue; // has neighbour

				const float* va = &tile->verts[poly->verts[ei] * 3];
				const float* vb = &tile->verts[poly->verts[(ei + 1) % poly->vertCount] * 3];

				float mid[3] = {
					( va[0] + vb[0] ) * 0.5f,
					( va[1] + vb[1] ) * 0.5f,
					( va[2] + vb[2] ) * 0.5f
				};

				float dx = vb[0] - va[0];
				float dz = vb[2] - va[2];
				float len = sqrtf( dx*dx + dz*dz );
				if ( len < 0.001f ) continue;

				// Outward normal: perpendicular to edge in XZ, pointing away from poly centroid
				float cx = 0.0f, cz = 0.0f;
				for ( int v = 0; v < poly->vertCount; ++v ) {
					cx += tile->verts[poly->verts[v] * 3 + 0];
					cz += tile->verts[poly->verts[v] * 3 + 2];
				}
				cx /= poly->vertCount;
				cz /= poly->vertCount;

				float nx =  dz / len, nz = -dx / len;
				if ( nx * (mid[0] - cx) + nz * (mid[2] - cz) < 0.0f ) {
					nx = -nx; nz = -nz;
				}

				BoundaryEdge edge;
				edge.mid[0] = mid[0]; edge.mid[1] = mid[1]; edge.mid[2] = mid[2];
				edge.normal[0] = nx;  edge.normal[1] = 0.0f; edge.normal[2] = nz;
				edges.push_back( edge );
			}
		}
	}
	return edges;
}

// -----------------------------------------------------------------------
// Pass 1 — Drop Connections (AREA_JUMP_DROP)
// Ledges where the player walks off and falls; no jump input required.
// -----------------------------------------------------------------------
static void Nav_DetectDropConnections( dtNavMeshQuery* query, const dtNavMesh* mesh,
                                       const std::vector<BoundaryEdge>& edges ) {
	const float scale = useMeters ? 0.0254f : 1.0f;

	const float ESCAPE_DIST  = 16.0f  * scale; // clearance probe past the ledge
	const float MAX_DROP     = 370.0f * scale;  // max fall distance (~769 u/s impact)
	const float MIN_DROP     = 22.0f  * scale;  // STEPSIZE(18)+4 — auto-step handles less
	const float LAND_RAD_XZ  = 48.0f  * scale;  // findNearestPoly XZ search radius
	const float LAND_RAD_Y   = 32.0f  * scale;  // findNearestPoly Y search half-extent
	const float CONN_RAD     = 24.0f  * scale;  // off-mesh activation radius
	const float MIN_UP_NORMAL = 0.7f;            // landing surface must face up

	dtQueryFilter filter;
	filter.setIncludeFlags( 0xFFFF );
	filter.setExcludeFlags( 0 );

	int dropCount = 0;
	int dbg_wall = 0, dbg_noground = 0, dbg_normal = 0, dbg_delta_low = 0, dbg_delta_high = 0, dbg_nopoly = 0, dbg_snapdelta = 0, dbg_hurt = 0;

	for ( const BoundaryEdge& edge : edges ) {
		const float* M = edge.mid;
		const float* N = edge.normal;

		// 1. Escape check — horizontal ray 16 units outward from ledge midpoint.
		//    If it hits something the edge is wall-adjacent, not an open ledge.
		float escEnd[3] = { M[0] + N[0]*ESCAPE_DIST, M[1], M[2] + N[2]*ESCAPE_DIST };
		HitResult esc = Nav_TraceRayHit( M, escEnd );
		if ( esc.hit ) { dbg_wall++; continue; }

		// P = point in open space just past the ledge
		float P[3] = { escEnd[0], escEnd[1], escEnd[2] };

		// 2. Downward ray from P for up to MAX_DROP.
		//    The navmesh cell-height grid often creates a 1-2 unit outer rim just
		//    below the surface boundary. When the first hit is within LIP_SKIP units
		//    and faces upward, step past it — the real landing platform is below.
		//    LIP_SKIP is kept small (8 units) so we only skip genuine grid artifacts,
		//    not actual close floors in indoor maps.
		const float LIP_SKIP = 8.0f * scale;
		HitResult ground;
		float searchStart[3] = { P[0], P[1], P[2] };
		float totalSearched = 0.0f;
		bool foundGround = false;
		while ( totalSearched < MAX_DROP ) {
			float remaining = MAX_DROP - totalSearched;
			float downEnd[3] = { searchStart[0], searchStart[1] - remaining, searchStart[2] };
			ground = Nav_TraceRayHit( searchStart, downEnd );
			if ( !ground.hit ) { break; }

			float stepDelta = searchStart[1] - ground.point[1];
			totalSearched += stepDelta;

			// Skip only thin outer lips (< LIP_SKIP) with upward normals.
			if ( stepDelta < LIP_SKIP && ground.normal[1] > MIN_UP_NORMAL ) {
				searchStart[0] = ground.point[0];
				searchStart[1] = ground.point[1] - 1.0f * scale;
				searchStart[2] = ground.point[2];
				continue;
			}
			foundGround = true;
			break;
		}
		if ( !foundGround ) { dbg_noground++; continue; }

		const float* H = ground.point;

		// 3. Landing surface must face upward
		if ( ground.normal[1] < MIN_UP_NORMAL ) { dbg_normal++; continue; }

		// 4. Height delta from the original ledge: must be a meaningful drop but not lethal
		float delta = M[1] - H[1];
		if ( delta < MIN_DROP ) { dbg_delta_low++; continue; }
		if ( delta > MAX_DROP ) { dbg_delta_high++; continue; }

		// 5. Navmesh confirmation at landing point
		float snapExt[3] = { LAND_RAD_XZ, LAND_RAD_Y, LAND_RAD_XZ };
		dtPolyRef landRef = 0;
		float landSnapped[3];
		query->findNearestPoly( H, snapExt, &filter, &landRef, landSnapped );
		if ( !landRef ) { dbg_nopoly++; continue; }

		// Verify the snapped landing point is still meaningfully below the ledge.
		// findNearestPoly can pull the point up to the ledge polygon itself when
		// the raw hit is near a shared edge, producing zero-distance connections.
		float snapDelta = M[1] - landSnapped[1];
		if ( snapDelta < MIN_DROP ) { dbg_snapdelta++; continue; }

		// 6. Reject landing points inside trigger_hurt volumes (pits, void floors).
		bool inHurt = false;
		for ( const HurtVolume& hv : hurtVolumes ) {
			if ( landSnapped[0] >= hv.mins[0] && landSnapped[0] <= hv.maxs[0] &&
			     landSnapped[1] >= hv.mins[1] && landSnapped[1] <= hv.maxs[1] &&
			     landSnapped[2] >= hv.mins[2] && landSnapped[2] <= hv.maxs[2] ) {
				inHurt = true;
				break;
			}
		}
		if ( inHurt ) { dbg_hurt++; continue; }

		// 6. Emit one-way off-mesh connection
		offMeshConVerts.push_back( M[0] );
		offMeshConVerts.push_back( M[1] );
		offMeshConVerts.push_back( M[2] );
		offMeshConVerts.push_back( landSnapped[0] );
		offMeshConVerts.push_back( landSnapped[1] );
		offMeshConVerts.push_back( landSnapped[2] );

		offMeshConRad.push_back( CONN_RAD );
		offMeshConDir.push_back( 0 );           // one-way
		offMeshConAreas.push_back( AREA_JUMP_DROP );
		offMeshConFlags.push_back( 1 );
		offMeshConUserID.push_back( 0 );
		dropCount++;
	}

	Sys_Printf( "Drop connections: %d  (filtered: wall=%d no-ground=%d normal=%d delta-low=%d delta-high=%d no-poly=%d snap=%d hurt=%d)\n",
	            dropCount, dbg_wall, dbg_noground, dbg_normal, dbg_delta_low, dbg_delta_high, dbg_nopoly, dbg_snapdelta, dbg_hurt );
}

static bool Nav_CheckLOS(const float* start, const float* end) {
	ray_t ray;
	vec3_t dir;
	VectorSubtract(end, start, dir);
	float dist = VectorLength(dir);
	if (dist < 0.01f) return true;
	VectorScale(dir, 1.0f/dist, dir);
	ray_construct_for_vec3(&ray, start, dir);

	const rcChunkyTriMesh* chunky = geo.getChunkyMesh();
	const float* verts = geo.getVerts();
	
	float p[2] = { start[0], start[2] };
	float q[2] = { end[0], end[2] };
	
	int ids[512];
	int nids = rcGetChunksOverlappingSegment(chunky, p, q, ids, 512);
	
	for (int i = 0; i < nids; ++i) {
		const rcChunkyTriMeshNode& node = chunky->nodes[ids[i]];
		const int* tris = &chunky->tris[node.i * 3];
		const int ntris = node.n;
		
		for (int j = 0; j < ntris; ++j) {
			const float* v0 = &verts[tris[j * 3 + 0] * 3];
			const float* v1 = &verts[tris[j * 3 + 1] * 3];
			const float* v2 = &verts[tris[j * 3 + 2] * 3];
			
			float t = ray_intersect_triangle(&ray, qfalse, v0, v1, v2);
			if (t > 0.1f && t < dist - 0.1f) {
				return false;
			}
		}
	}
	return true;
}

struct LandingPoint {
	float pos[3];
};

// -----------------------------------------------------------------------
// Pass 2 — Basic Jump Connections (AREA_JUMP_BASIC)
// Running jump across a gap.  The player jumps at JUMP_VELOCITY vertical
// while moving at RUN_SPEED horizontal.  We probe outward from every open
// boundary edge in PROBE_STEP increments, looking for a landing platform
// across a gap that the JKA jump arc can actually reach.
// -----------------------------------------------------------------------
static void Nav_DetectJumpConnections( dtNavMeshQuery* query, const dtNavMesh* mesh,
                                       const std::vector<BoundaryEdge>& edges ) {
	const float scale = useMeters ? 0.0254f : 1.0f;

	// JKA physics (Quake units → scaled)
	const float JUMP_VEL        = 270.0f * scale;   // vertical takeoff velocity
	const float GRAVITY         = 800.0f * scale;   // gravitational acceleration
	const float RUN_SPEED       = 250.0f * scale;   // horizontal speed while in air
	const float MAX_HEIGHT_GAIN = 45.0f  * scale;   // JUMP_VEL²/(2·GRAVITY) ≈ 45.6 u
	const float MAX_DROP_DIST   = 370.0f * scale;   // lethal fall threshold (downcast only)
	const float MAX_JUMP_DROP   =  48.0f * scale;   // Pass 2 delta_h lower bound — larger falls belong to Pass 1
	const float HEAD_HEIGHT     = 56.0f  * scale;   // LOS eye-level clearance
	const float MIN_GAP_START   = 26.0f  * scale;   // begin probing past ledge edge
	const float PROBE_STEP      =  8.0f  * scale;   // horizontal probe increment
	const float MIN_STEP        = 22.0f  * scale;   // STEPSIZE+4: sub-step can just walk
	const float CONN_RAD        = 32.0f  * scale;   // off-mesh activation radius
	const float LAND_RAD_XZ     = 48.0f  * scale;   // findNearestPoly XZ half-extent
	const float LAND_RAD_Y      = 32.0f  * scale;   // findNearestPoly Y half-extent
	const float MIN_UP_NORMAL   = 0.7f;
	const float DEDUP_RADIUS    = 64.0f * scale;   // skip-ahead after emitting a connection

	// Compute the maximum horizontal distance the arc can cover (flat jump).
	// flight_time at delta_h=0:  t = 2*JUMP_VEL / GRAVITY
	const float FLAT_FLIGHT_TIME = 2.0f * JUMP_VEL / GRAVITY;
	const float MAX_HORIZONTAL   = RUN_SPEED * FLAT_FLIGHT_TIME; // ~168 u

	dtQueryFilter filter;
	filter.setIncludeFlags( 0xFFFF );
	filter.setExcludeFlags( 0 );

	int jumpCount = 0;
	// Note: debug counters for inner loop accumulate per probe step, not per edge.
	int dbg_wall = 0, dbg_nopoly_src = 0;
	int dbg_noground = 0, dbg_physics = 0, dbg_los = 0;
	int dbg_nopoly_dst = 0, dbg_snapdelta = 0, dbg_hurt = 0;

	for ( const BoundaryEdge& edge : edges ) {
		const float* M = edge.mid;
		const float* N = edge.normal;

		// 1. Escape-space check: is there at least MIN_GAP_START of open air ahead?
		//    A wall within that range means we can't run/jump from here.
		float escEnd[3] = { M[0] + N[0]*MIN_GAP_START, M[1], M[2] + N[2]*MIN_GAP_START };
		HitResult esc = Nav_TraceRayHit( M, escEnd );
		if ( esc.hit ) { dbg_wall++; continue; }

		// 2. Source must be on the navmesh (sanity — boundary edges should be, but check).
		float snapExt[3] = { LAND_RAD_XZ, LAND_RAD_Y, LAND_RAD_XZ };
		dtPolyRef srcRef = 0;
		float srcSnapped[3];
		query->findNearestPoly( M, snapExt, &filter, &srcRef, srcSnapped );
		if ( !srcRef ) { dbg_nopoly_src++; continue; }

		// 3. Probe outward in steps; at each position look for a gap then landing floor.
		for ( float dist = MIN_GAP_START; dist <= MAX_HORIZONTAL; dist += PROBE_STEP ) {
			float X[3] = { M[0] + N[0]*dist, M[1], M[2] + N[2]*dist };

			// Gap check: if there is solid floor within STEPSIZE just below M.y at X,
			// the player can simply walk there — no jump connection needed at this step.
			float gapCheckEnd[3] = { X[0], M[1] - MIN_STEP, X[2] };
			HitResult gapCheck = Nav_TraceRayHit( X, gapCheckEnd );
			if ( gapCheck.hit ) continue; // floor continues, not a gap

			// Cast down from X to find the landing platform.
			float downEnd[3] = { X[0], X[1] - MAX_DROP_DIST, X[2] };
			HitResult ground = Nav_TraceRayHit( X, downEnd );
			if ( !ground.hit ) { dbg_noground++; continue; }
			if ( ground.normal[1] < MIN_UP_NORMAL ) continue;

			float delta_h = ground.point[1] - M[1]; // positive = target is higher

			// Vertical reachability: too high to jump up, or below the pass-2 drop cap.
			// Large drops (> 48u) are handled by Pass 1; keep Pass 2 as short gap-hops.
			if ( delta_h >  MAX_HEIGHT_GAIN ) { dbg_physics++; continue; }
			if ( delta_h < -MAX_JUMP_DROP   ) { dbg_physics++; continue; }

			// Compute flight time for this delta_h using projectile equation:
			//   0 = JUMP_VEL·t – ½·g·t² – delta_h
			//   t = (JUMP_VEL + √(JUMP_VEL² – 2·g·delta_h)) / g
			float disc = JUMP_VEL*JUMP_VEL - 2.0f*GRAVITY*delta_h;
			if ( disc < 0.0f ) { dbg_physics++; continue; } // can't reach height
			float flight_time = ( JUMP_VEL + sqrtf( disc ) ) / GRAVITY;
			float max_xz      = RUN_SPEED * flight_time;

			// Horizontal reachability: distance must be within the arc's range.
			if ( dist > max_xz ) { dbg_physics++; continue; }

			// LOS check at head height: no wall between M and X at eye level.
			float losStart[3] = { M[0],    M[1]             + HEAD_HEIGHT, M[2] };
			float losEnd[3]   = { X[0], ground.point[1]     + HEAD_HEIGHT, X[2] };
			if ( !Nav_CheckLOS( losStart, losEnd ) ) { dbg_los++; continue; }

			// Snap landing point to navmesh.
			dtPolyRef landRef = 0;
			float landSnapped[3];
			query->findNearestPoly( ground.point, snapExt, &filter, &landRef, landSnapped );
			if ( !landRef ) { dbg_nopoly_dst++; continue; }

			// Verify snap didn't pull the point back up to source level.
			// fabsf so we handle both upward and downward targets.
			if ( fabsf( landSnapped[1] - M[1] ) < MIN_STEP && dist > MIN_STEP ) {
				dbg_snapdelta++; continue;
			}

			// Zero-distance guard (start == end after snap).
			{
				float ddx = landSnapped[0]-M[0], ddy = landSnapped[1]-M[1], ddz = landSnapped[2]-M[2];
				if ( ddx*ddx + ddy*ddy + ddz*ddz < (MIN_STEP*MIN_STEP) ) {
					dbg_snapdelta++; continue;
				}
			}

			// Reject landing inside trigger_hurt volumes.
			bool inHurt = false;
			for ( const HurtVolume& hv : hurtVolumes ) {
				if ( landSnapped[0] >= hv.mins[0] && landSnapped[0] <= hv.maxs[0] &&
				     landSnapped[1] >= hv.mins[1] && landSnapped[1] <= hv.maxs[1] &&
				     landSnapped[2] >= hv.mins[2] && landSnapped[2] <= hv.maxs[2] ) {
					inHurt = true; break;
				}
			}
			if ( inHurt ) { dbg_hurt++; continue; }

			// Bidirectional check: can the same jump be made in reverse?
			// Reverse: from landSnapped toward M, so delta_h flips sign.
			unsigned char connDir = 0; // one-way by default
			{
				float back_delta_h = M[1] - landSnapped[1];
				float back_disc    = JUMP_VEL*JUMP_VEL - 2.0f*GRAVITY*back_delta_h;
				if ( back_disc >= 0.0f ) {
					float back_flight = ( JUMP_VEL + sqrtf( back_disc ) ) / GRAVITY;
					float back_max_xz = RUN_SPEED * back_flight;
					if ( dist <= back_max_xz ) {
						float bLosS[3] = { landSnapped[0], landSnapped[1]+HEAD_HEIGHT, landSnapped[2] };
						float bLosE[3] = { M[0],           M[1]+HEAD_HEIGHT,           M[2] };
						if ( Nav_CheckLOS( bLosS, bLosE ) ) {
							connDir = 1; // bidirectional
						}
					}
				}
			}

			offMeshConVerts.push_back( M[0] );
			offMeshConVerts.push_back( M[1] );
			offMeshConVerts.push_back( M[2] );
			offMeshConVerts.push_back( landSnapped[0] );
			offMeshConVerts.push_back( landSnapped[1] );
			offMeshConVerts.push_back( landSnapped[2] );
			offMeshConRad.push_back  ( CONN_RAD );
			offMeshConDir.push_back  ( connDir );
			offMeshConAreas.push_back( AREA_JUMP_BASIC );
			offMeshConFlags.push_back( 1 );
			offMeshConUserID.push_back( 0 );
			jumpCount++;

			// Skip ahead by DEDUP_RADIUS: nearby probe steps would produce near-duplicate
			// connections that the dedup pass removes anyway.  This dramatically reduces
			// generation cost for large maps without losing unique landing spots.
			dist += DEDUP_RADIUS - PROBE_STEP; // for-loop adds PROBE_STEP next iteration
		}
	}

	Sys_Printf( "Jump connections: %d  (filtered: wall=%d no-src=%d no-ground=%d physics=%d los=%d no-dst=%d snap=%d hurt=%d)\n",
	            jumpCount, dbg_wall, dbg_nopoly_src, dbg_noground, dbg_physics, dbg_los,
	            dbg_nopoly_dst, dbg_snapdelta, dbg_hurt );
}

// -----------------------------------------------------------------------
// Pass 3 — Wallrun Ascend Connections (AREA_WALLRUN_ASCEND)
// JKA players can run along a near-vertical wall surface and ascend to
// reach a platform above them that is otherwise unreachable by a normal
// jump.  Wall height must be 45–226 Quake units.
// -----------------------------------------------------------------------
static void Nav_DetectWallrunConnections( dtNavMeshQuery* query, const dtNavMesh* mesh,
                                          const std::vector<BoundaryEdge>& edges ) {
	const float scale = useMeters ? 0.0254f : 1.0f;

	const float WALLRUN_MIN_H   =  45.0f * scale;  // min climbable height (= MAX_HEIGHT_GAIN)
	const float WALLRUN_MAX_H   = 450.0f * scale;  // JKA sustained wallrun cap (~376u elevator span + margin)
	const float WALL_PROBE      =  32.0f * scale;  // outward probe to detect wall face
	const float WALL_VERT_MAX   =  0.2f;           // |normal_y| threshold for near-vertical
	const float WALL_PULLBACK   =   1.0f * scale;  // pull back from wall face to avoid brush seam
	const float OVER_LEDGE_DIST =  24.0f * scale;  // short ceiling-check probe at apex height
	const float FLOOR_SWEEP_DIST= 192.0f * scale;  // max inward sweep to find landing floor
	const float START_SETBACK   =  16.0f * scale;  // pull start back toward poly interior
	const float CONN_RAD        =  48.0f * scale;
	const float LAND_RAD_XZ     =  48.0f * scale;
	const float LAND_RAD_Y      =  32.0f * scale;
	const float MIN_UP_NORMAL   =  0.7f;

	dtQueryFilter filter;
	filter.setIncludeFlags( 0xFFFF );
	filter.setExcludeFlags( 0 );

	int wallrunCount = 0;
	int dbg_nowall = 0, dbg_angle = 0, dbg_height = 0;
	int dbg_notop = 0, dbg_nopoly_src = 0, dbg_nopoly_dst = 0;
	int dbg_blocked = 0, dbg_hurt = 0;

	// Targeted diagnostic: trace why edges near the travel-376 elevator walls are rejected.
	// Recast-space (Y-up) reference positions — elevator *12 and *21 floor level.
	// Quake: *12 bottom=(896,2400,-342), *21 bottom=(4384,-1088,-342)  →  Recast Y=Quake-Z.
	const float dbgE12[3] = {  896.0f * scale, -342.0f * scale, 2400.0f * scale };
	const float dbgE21[3] = { 4384.0f * scale, -342.0f * scale, -1088.0f * scale };
	const float DBG_RAD  = 400.0f * scale;   // XZ search radius (Quake units)
	const float DBG_RADY = 500.0f * scale;   // Y search radius (wide to catch any floor level near elevator)

	auto elevDbgName = [&]( const float* pt ) -> const char* {
		float dx, dy, dz;
		dx=pt[0]-dbgE12[0]; dy=pt[1]-dbgE12[1]; dz=pt[2]-dbgE12[2];
		if ( dx*dx+dz*dz < DBG_RAD*DBG_RAD && fabsf(dy) < DBG_RADY ) return "*12";
		dx=pt[0]-dbgE21[0]; dy=pt[1]-dbgE21[1]; dz=pt[2]-dbgE21[2];
		if ( dx*dx+dz*dz < DBG_RAD*DBG_RAD && fabsf(dy) < DBG_RADY ) return "*21";
		return nullptr;
	};

	for ( const BoundaryEdge& edge : edges ) {
		const float* M = edge.mid;
		const float* N = edge.normal;

		const char* eDbg = elevDbgName( M );
		if ( eDbg ) {
			vec3_t gM; TransformPointToGame( M, gM );
			Sys_Printf( "  [ELEV-DBG %s] Edge mid=(%.0f,%.0f,%.0f) N=(%.3f,%.3f)\n",
			            eDbg, gM[0], gM[1], gM[2], N[0], N[2] );
		}

		// 1. Probe outward to detect a near-vertical wall surface.
		float wallEnd[3] = { M[0] + N[0]*WALL_PROBE, M[1], M[2] + N[2]*WALL_PROBE };
		HitResult wallHit = Nav_TraceRayHit( M, wallEnd );
		if ( !wallHit.hit ) {
			dbg_nowall++;
			if ( eDbg ) {
				vec3_t gM; TransformPointToGame( M, gM );
				Sys_Printf( "  [ELEV-DBG %s]   FAIL no-wall: mid=(%.0f,%.0f,%.0f) probe-dir=(%.3f,%.3f)\n",
				            eDbg, gM[0], gM[1], gM[2], N[0], N[2] );
			}
			continue;
		}

		// Wall must be near-vertical.
		if ( fabsf( wallHit.normal[1] ) > WALL_VERT_MAX ) {
			dbg_angle++;
			if ( eDbg ) {
				vec3_t gM; TransformPointToGame( M, gM );
				Sys_Printf( "  [ELEV-DBG %s]   FAIL angle: wall normalY=%.3f (max %.2f)\n",
				            eDbg, wallHit.normal[1], WALL_VERT_MAX );
			}
			continue;
		}

		// 2. Pull back 1u from the wall face to avoid brush-seam floating-point issues.
		float traceOrigin[3] = {
			wallHit.point[0] + wallHit.normal[0] * WALL_PULLBACK,
			M[1],
			wallHit.point[2] + wallHit.normal[2] * WALL_PULLBACK
		};

		// 3. Cast UPWARD from traceOrigin up to WALLRUN_MAX_H.
		//    If a ceiling is hit first, the apex is clamped there — no skybox bleed.
		float upEnd[3] = { traceOrigin[0], traceOrigin[1] + WALLRUN_MAX_H, traceOrigin[2] };
		HitResult upHit = Nav_TraceRayHit( traceOrigin, upEnd );
		float apex[3];
		if ( upHit.hit ) {
			// Ceiling found: clamp apex just below it.
			apex[0] = upHit.point[0];
			apex[1] = upHit.point[1] - 1.0f * scale;
			apex[2] = upHit.point[2];
			if ( eDbg ) {
				float ceilH = (upHit.point[1] - M[1]) / scale;
				Sys_Printf( "  [ELEV-DBG %s]   upcast HIT ceiling at h=%.0f Qu (apex clamped)\n",
				            eDbg, ceilH );
			}
		} else {
			apex[0] = upEnd[0];
			apex[1] = upEnd[1];
			apex[2] = upEnd[2];
			if ( eDbg ) {
				Sys_Printf( "  [ELEV-DBG %s]   upcast clear to h=%.0f Qu\n",
				            eDbg, WALLRUN_MAX_H / scale );
			}
		}

		float wallHeight = apex[1] - M[1];
		if ( wallHeight < WALLRUN_MIN_H ) {
			dbg_height++;
			if ( eDbg ) {
				Sys_Printf( "  [ELEV-DBG %s]   FAIL height-low: h=%.1f Qu (min=%.0f)\n",
				            eDbg, wallHeight/scale, WALLRUN_MIN_H/scale );
			}
			continue;
		}
		// wallHeight > WALLRUN_MAX_H is impossible by construction (capped by upEnd).

		// 4. Short ceiling check: probe OVER_LEDGE_DIST inward at apex height.
		//    If blocked immediately, the wall goes all the way to ceiling — dead end.
		float ceilCheckEnd[3] = {
			apex[0] - N[0] * OVER_LEDGE_DIST,
			apex[1],
			apex[2] - N[2] * OVER_LEDGE_DIST
		};
		HitResult ceilHit = Nav_TraceRayHit( apex, ceilCheckEnd );
		if ( ceilHit.hit ) {
			dbg_notop++;
			if ( eDbg ) {
				Sys_Printf( "  [ELEV-DBG %s]   FAIL notop: short inward probe blocked (wall goes to ceiling)\n",
				            eDbg );
			}
			continue; // wall has no accessible top
		}

		// 5. Floor-finding sweep: probe inward up to FLOOR_SWEEP_DIST, then downward.
		//    The landing platform may be well back from the wall face (e.g. elevator shaft),
		//    so we sweep much farther than the ceiling check.
		float sweepEnd[3] = {
			apex[0] - N[0] * FLOOR_SWEEP_DIST,
			apex[1],
			apex[2] - N[2] * FLOOR_SWEEP_DIST
		};
		HitResult sweepHit = Nav_TraceRayHit( apex, sweepEnd );
		float floorOrigin[3];
		if ( sweepHit.hit ) {
			// Inward wall encountered: probe downward just before it.
			floorOrigin[0] = sweepHit.point[0] + sweepHit.normal[0] * 2.0f * scale;
			floorOrigin[1] = apex[1];
			floorOrigin[2] = sweepHit.point[2] + sweepHit.normal[2] * 2.0f * scale;
			if ( eDbg ) {
				float sweepD = sqrtf(
					(sweepHit.point[0]-apex[0])*(sweepHit.point[0]-apex[0]) +
					(sweepHit.point[2]-apex[2])*(sweepHit.point[2]-apex[2]) );
				Sys_Printf( "  [ELEV-DBG %s]   sweep hit inward wall at d=%.0f Qu\n",
				            eDbg, sweepD/scale );
			}
		} else {
			floorOrigin[0] = sweepEnd[0];
			floorOrigin[1] = sweepEnd[1];
			floorOrigin[2] = sweepEnd[2];
			if ( eDbg ) {
				Sys_Printf( "  [ELEV-DBG %s]   sweep clear %.0f Qu inward\n", eDbg, FLOOR_SWEEP_DIST/scale );
			}
		}

		float floorEnd[3] = { floorOrigin[0], floorOrigin[1] - WALLRUN_MAX_H, floorOrigin[2] };
		HitResult topFloor = Nav_TraceRayHit( floorOrigin, floorEnd );
		if ( !topFloor.hit ) {
			dbg_notop++;
			if ( eDbg ) {
				Sys_Printf( "  [ELEV-DBG %s]   FAIL notop: downward probe found no floor\n", eDbg );
			}
			continue;
		}
		if ( topFloor.normal[1] < MIN_UP_NORMAL ) {
			dbg_notop++;
			if ( eDbg ) {
				Sys_Printf( "  [ELEV-DBG %s]   FAIL notop: floor normalY=%.3f (min %.2f)\n",
				            eDbg, topFloor.normal[1], MIN_UP_NORMAL );
			}
			continue;
		}

		// Re-validate height against the actual floor hit.
		wallHeight = topFloor.point[1] - M[1];
		if ( wallHeight < WALLRUN_MIN_H ) {
			dbg_height++;
			if ( eDbg ) {
				Sys_Printf( "  [ELEV-DBG %s]   FAIL height-low (floor): h=%.1f Qu\n",
				            eDbg, wallHeight/scale );
			}
			continue;
		}
		if ( wallHeight > WALLRUN_MAX_H ) {
			dbg_height++;
			if ( eDbg ) {
				Sys_Printf( "  [ELEV-DBG %s]   FAIL height-high: h=%.1f Qu (max=%.0f)\n",
				            eDbg, wallHeight/scale, WALLRUN_MAX_H/scale );
			}
			continue;
		}

		if ( eDbg ) {
			vec3_t gFloor; TransformPointToGame( topFloor.point, gFloor );
			Sys_Printf( "  [ELEV-DBG %s]   height OK: h=%.0f Qu landing=(%.0f,%.0f,%.0f)\n",
			            eDbg, wallHeight/scale, gFloor[0], gFloor[1], gFloor[2] );
		}

		// 6. Headroom check: cast a ray straight up from the approach point by
		//    wallHeight.  An obstruction (overhang, sign, low ceiling) means the
		//    bot can't physically run up this wall.
		float startPt[3] = {
			M[0] - N[0] * START_SETBACK,
			M[1],
			M[2] - N[2] * START_SETBACK
		};
		float headEnd[3] = { startPt[0], startPt[1] + wallHeight, startPt[2] };
		HitResult headHit = Nav_TraceRayHit( startPt, headEnd );
		if ( headHit.hit ) {
			dbg_blocked++;
			if ( eDbg ) {
				float hitH = (headHit.point[1] - M[1]) / scale;
				Sys_Printf( "  [ELEV-DBG %s]   FAIL headroom: obstruction at h=%.0f Qu\n",
				            eDbg, hitH );
			}
			continue;
		}

		// 7. Snap landing and start points to navmesh.
		float snapExt[3] = { LAND_RAD_XZ, LAND_RAD_Y, LAND_RAD_XZ };
		dtPolyRef landRef = 0;
		float landSnapped[3];
		query->findNearestPoly( topFloor.point, snapExt, &filter, &landRef, landSnapped );
		if ( !landRef ) {
			dbg_nopoly_dst++;
			if ( eDbg ) {
				vec3_t gF; TransformPointToGame( topFloor.point, gF );
				Sys_Printf( "  [ELEV-DBG %s]   FAIL no-dst-poly: landing (%.0f,%.0f,%.0f) not on navmesh\n",
				            eDbg, gF[0], gF[1], gF[2] );
			}
			continue;
		}

		dtPolyRef startRef = 0;
		float startSnapped[3];
		query->findNearestPoly( startPt, snapExt, &filter, &startRef, startSnapped );
		if ( !startRef ) {
			dbg_nopoly_src++;
			if ( eDbg ) {
				vec3_t gS; TransformPointToGame( startPt, gS );
				Sys_Printf( "  [ELEV-DBG %s]   FAIL no-src-poly: start (%.0f,%.0f,%.0f) not on navmesh\n",
				            eDbg, gS[0], gS[1], gS[2] );
			}
			continue;
		}

		// Zero-distance guard.
		{
			float ddx = landSnapped[0]-startSnapped[0];
			float ddy = landSnapped[1]-startSnapped[1];
			float ddz = landSnapped[2]-startSnapped[2];
			if ( ddx*ddx + ddy*ddy + ddz*ddz < (WALLRUN_MIN_H*WALLRUN_MIN_H) ) continue;
		}

		// Reject landing inside trigger_hurt volumes.
		bool inHurt = false;
		for ( const HurtVolume& hv : hurtVolumes ) {
			if ( landSnapped[0] >= hv.mins[0] && landSnapped[0] <= hv.maxs[0] &&
			     landSnapped[1] >= hv.mins[1] && landSnapped[1] <= hv.maxs[1] &&
			     landSnapped[2] >= hv.mins[2] && landSnapped[2] <= hv.maxs[2] ) {
				inHurt = true; break;
			}
		}
		if ( inHurt ) { dbg_hurt++; continue; }

		{
			vec3_t gStart, gEnd;
			TransformPointToGame( startSnapped, gStart );
			TransformPointToGame( landSnapped,  gEnd );
			Sys_Printf( "  Wallrun: (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f) h=%.0f\n",
			            gStart[0], gStart[1], gStart[2],
			            gEnd[0],   gEnd[1],   gEnd[2],
			            wallHeight / scale );
		}

		offMeshConVerts.push_back( startSnapped[0] );
		offMeshConVerts.push_back( startSnapped[1] );
		offMeshConVerts.push_back( startSnapped[2] );
		offMeshConVerts.push_back( landSnapped[0]  );
		offMeshConVerts.push_back( landSnapped[1]  );
		offMeshConVerts.push_back( landSnapped[2]  );
		offMeshConRad.push_back  ( CONN_RAD );
		offMeshConDir.push_back  ( 0 );                // one-way: can't wallrun down
		offMeshConAreas.push_back( AREA_WALLRUN_ASCEND );
		offMeshConFlags.push_back( 1 );
		offMeshConUserID.push_back( 0 );
		wallrunCount++;
	}

	Sys_Printf( "Wallrun connections: %d  (filtered: no-wall=%d bad-angle=%d height=%d no-top=%d blocked=%d no-src=%d no-dst=%d hurt=%d)\n",
	            wallrunCount, dbg_nowall, dbg_angle, dbg_height, dbg_notop,
	            dbg_blocked, dbg_nopoly_src, dbg_nopoly_dst, dbg_hurt );
}

// -----------------------------------------------------------------------
// Nav_LoadSidecarConnections
// Reads hand-authored off-mesh connections from <mapname>.nav_connections.
// Format (one entry per line, # comments, whitespace-delimited):
//   type  sx  sy  sz  ex  ey  ez  bidir  radius  area
// Coordinates are in Quake space (Z-up, Quake units).
// Deduplication is done per (startPolyRef, endPolyRef) pair so that
// densely-sampled sidecar entries that snap to the same poly pair are
// only emitted once.  Cross-pass geometric dedup runs afterwards.
// -----------------------------------------------------------------------
static void Nav_LoadSidecarConnections( dtNavMeshQuery* query )
{
	// Derive sidecar path: replace .bsp extension with .nav_connections
	char sidecarPath[1024];
	strncpy( sidecarPath, source, sizeof(sidecarPath) - 1 );
	sidecarPath[sizeof(sidecarPath)-1] = '\0';
	char *ext = strrchr( sidecarPath, '.' );
	if ( ext ) {
		strcpy( ext, ".nav_connections" );
	} else {
		strncat( sidecarPath, ".nav_connections", sizeof(sidecarPath) - strlen(sidecarPath) - 1 );
	}

	FILE *f = fopen( sidecarPath, "r" );
	if ( !f ) {
		// Not an error — sidecar is optional
		return;
	}
	Sys_Printf( "Loading sidecar connections from: %s\n", sidecarPath );

	const float scale = useMeters ? 0.0254f : 1.0f;
	const float WALLRUN_MIN_H_QU = 45.0f; // minimum height gain in Quake units

	// snap extents: generous XZ + Y to handle slightly misplaced hand-authored points
	float snapExt[3] = { 96.0f * scale, 128.0f * scale, 96.0f * scale };

	dtQueryFilter filter;
	filter.setIncludeFlags( 0xFFFF );
	filter.setExcludeFlags( 0 );

	// Per-polyref-pair dedup set: avoids pushing many identical connections
	// when a sidecar file has closely-spaced start/end points that snap to the same polys.
	std::set< std::pair<dtPolyRef,dtPolyRef> > seenPairs;

	int statLoaded = 0, statSkipDup = 0, statSkipNoPoly = 0, statSkipInvalid = 0, statLine = 0;

	char line[512];
	while ( fgets( line, sizeof(line), f ) ) {
		statLine++;

		// Strip leading whitespace
		char *p = line;
		while ( *p == ' ' || *p == '\t' ) p++;

		// Skip blank lines and comments
		if ( *p == '\0' || *p == '\n' || *p == '\r' || *p == '#' ) continue;

		// Parse: type sx sy sz ex ey ez bidir radius area
		char typeStr[32];
		float sx, sy, sz, ex, ey, ez, radius;
		int   bidir, area;

		int parsed = sscanf( p, "%31s %f %f %f %f %f %f %d %f %d",
		                     typeStr, &sx, &sy, &sz, &ex, &ey, &ez,
		                     &bidir, &radius, &area );
		if ( parsed != 10 ) {
			Sys_Printf( "  [sidecar] WARNING line %d: expected 10 fields, got %d — skipping\n",
			            statLine, parsed );
			statSkipInvalid++;
			continue;
		}

		// Warn about suspiciously small height gains (likely misplaced scanner output)
		float dz = ez - sz; // Quake Z-up: height gain
		if ( dz < WALLRUN_MIN_H_QU ) {
			Sys_Printf( "  [sidecar] WARNING line %d: dz=%.1f < min=%.0f — likely bad entry, skipping\n",
			            statLine, dz, WALLRUN_MIN_H_QU );
			statSkipInvalid++;
			continue;
		}

		// Elevator crush check: if the start point falls inside any elevator's travel
		// column, nudge it along the wall face until it clears.
		// Wall-parallel direction = XY perpendicular to the approach vector (end - start).
		// We try both ± directions and take the first that clears all elevator volumes
		// AND still snaps to a valid navmesh poly.
		{
			float approachX = ex - sx;
			float approachY = ey - sy;
			// Normalize approach in XY (Quake XY is horizontal)
			float approachLen = sqrtf( approachX*approachX + approachY*approachY );
			if ( approachLen > 0.01f ) {
				approachX /= approachLen;
				approachY /= approachLen;
			}
			// Wall-parallel: rotate approach 90° in XY
			float wallParX = -approachY;
			float wallParY =  approachX;

			const float NUDGE_STEP = 16.0f;
			const float NUDGE_MAX  = 192.0f;

			auto insideAnyElevator = [&]( float px, float py, float pz ) -> bool {
				for ( const ElevatorVolume &ev : elevatorVolumes ) {
					if ( px >= ev.minx && px <= ev.maxx &&
					     py >= ev.miny && py <= ev.maxy &&
					     pz >= ev.minz && pz <= ev.maxz ) {
						return true;
					}
				}
				return false;
			};

			if ( insideAnyElevator( sx, sy, sz ) ) {
				bool nudged = false;
				for ( int dir = 0; dir < 2 && !nudged; dir++ ) {
					float sign = ( dir == 0 ) ? 1.0f : -1.0f;
					for ( float dist = NUDGE_STEP; dist <= NUDGE_MAX; dist += NUDGE_STEP ) {
						float nx = sx + sign * wallParX * dist;
						float ny = sy + sign * wallParY * dist;
						if ( insideAnyElevator( nx, ny, sz ) ) continue;

						// Check that this nudged position is still on the navmesh.
						vec3_t testPt = { nx, ny, sz };
						TransformPointToRecast( testPt );
						dtPolyRef testRef = 0;
						float testSnapped[3];
						query->findNearestPoly( testPt, snapExt, &filter, &testRef, testSnapped );
						if ( !testRef ) continue;

						Sys_Printf( "  [sidecar] line %d: start nudged %.0fu along wall (dir %c) to clear elevator — (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f)\n",
						            statLine, dist, dir == 0 ? '+' : '-', sx, sy, sz, nx, ny, sz );
						sx = nx;
						sy = ny;
						nudged = true;
						break;
					}
				}
				if ( !nudged ) {
					Sys_Printf( "  [sidecar] WARNING line %d: start (%.0f,%.0f,%.0f) inside elevator volume and could not be nudged clear — skipping\n",
					            statLine, sx, sy, sz );
					statSkipInvalid++;
					continue;
				}
			}
		}

		// Transform both endpoints to Recast space
		vec3_t startPt = { sx, sy, sz };
		vec3_t endPt   = { ex, ey, ez };
		TransformPointToRecast( startPt );
		TransformPointToRecast( endPt );

		// Snap start to nearest navmesh poly
		dtPolyRef startRef = 0;
		float startSnapped[3];
		query->findNearestPoly( startPt, snapExt, &filter, &startRef, startSnapped );
		if ( !startRef ) {
			Sys_Printf( "  [sidecar] line %d: start (%.0f,%.0f,%.0f) not on navmesh — skipping\n",
			            statLine, sx, sy, sz );
			statSkipNoPoly++;
			continue;
		}

		// Snap end to nearest navmesh poly
		dtPolyRef endRef = 0;
		float endSnapped[3];
		query->findNearestPoly( endPt, snapExt, &filter, &endRef, endSnapped );
		if ( !endRef ) {
			Sys_Printf( "  [sidecar] line %d: end (%.0f,%.0f,%.0f) not on navmesh — skipping\n",
			            statLine, ex, ey, ez );
			statSkipNoPoly++;
			continue;
		}

		// Dedup: check if this (start,end) pair has already been emitted
		auto pairFwd = std::make_pair( startRef, endRef );
		auto pairRev = std::make_pair( endRef, startRef );
		if ( seenPairs.count( pairFwd ) ) {
			statSkipDup++;
			continue;
		}
		seenPairs.insert( pairFwd );
		if ( bidir ) {
			// For bidirectional, also mark reverse so we don't emit it again
			seenPairs.insert( pairRev );
		}

		// Scale radius to Recast space
		float rcRadius = radius * scale;

		// Push the connection
		offMeshConVerts.push_back( startSnapped[0] );
		offMeshConVerts.push_back( startSnapped[1] );
		offMeshConVerts.push_back( startSnapped[2] );
		offMeshConVerts.push_back( endSnapped[0]   );
		offMeshConVerts.push_back( endSnapped[1]   );
		offMeshConVerts.push_back( endSnapped[2]   );

		offMeshConRad.push_back  ( rcRadius );
		offMeshConDir.push_back  ( (unsigned char)( bidir ? 1 : 0 ) );
		offMeshConAreas.push_back( (unsigned char)area );
		offMeshConFlags.push_back( 1 );

		statLoaded++;
	}

	fclose( f );

	Sys_Printf( "Sidecar connections: %d loaded, %d skipped-dup, %d skipped-no-poly, %d skipped-invalid  (from %d lines)\n",
	            statLoaded, statSkipDup, statSkipNoPoly, statSkipInvalid, statLine );
}

static void ExtractOffMeshConnections(dtNavMeshQuery* query, dtNavMesh* mesh) {
	offMeshConVerts.clear();
	offMeshConRad.clear();
	offMeshConDir.clear();
	offMeshConAreas.clear();
	offMeshConFlags.clear();
	offMeshConUserID.clear();
	hurtVolumes.clear();
	elevatorVolumes.clear();

	// --- Collect trigger_hurt volumes (Recast space) ---
	for ( int i = 0; i < numEntities; i++ ) {
		const entity_t *ent = &entities[i];
		if ( Q_stricmp( Nav_ValueForKey( ent, "classname" ), "trigger_hurt" ) != 0 ) continue;
		const char *modelStr = Nav_ValueForKey( ent, "model" );
		if ( modelStr[0] != '*' ) continue;
		int modelNum = atoi( modelStr + 1 );
		if ( modelNum < 0 || modelNum >= numBSPModels ) continue;
		const bspModel_t *model = &bspModels[modelNum];

		// Convert Quake AABB to Recast space (swap Y/Z, then scale if useMeters).
		const float hvScale = useMeters ? 0.0254f : 1.0f;
		HurtVolume hv;
		hv.mins[0] = model->mins[0] * hvScale;
		hv.mins[1] = model->mins[2] * hvScale; // Recast Y = Quake Z
		hv.mins[2] = model->mins[1] * hvScale; // Recast Z = Quake Y
		hv.maxs[0] = model->maxs[0] * hvScale;
		hv.maxs[1] = model->maxs[2] * hvScale;
		hv.maxs[2] = model->maxs[1] * hvScale;
		hurtVolumes.push_back( hv );
	}
	if ( !hurtVolumes.empty() ) {
		Sys_Printf( "  Found %d trigger_hurt volumes (will exclude landing points inside them)\n",
		            (int)hurtVolumes.size() );
	}

	dtQueryFilter filter;
	filter.setIncludeFlags(0xFFFF);
	filter.setExcludeFlags(0);

	const float minHeightJump = (useMeters ? 64.0f * 0.0254f : 64.0f);
	int elevatorCount = 0;
	int jumpPadCount = 0;

	for ( int i = 0; i < numEntities; i++ ) {
		entity_t *ent = &entities[i];
		const char *classname = Nav_ValueForKey( ent, "classname" );

		if ( !Q_stricmp( classname, "func_plat" ) ) {
			const char *modelStr = Nav_ValueForKey( ent, "model" );
			if ( modelStr[0] != '*' ) continue;

			int modelNum = atoi( &modelStr[1] );
			if ( modelNum < 0 || modelNum >= numBSPModels ) continue;

			bspModel_t *model = &bspModels[modelNum];

			// func_plat rests at its TOP position and drops DOWN by 'height'
			// when triggered. The BSP bounding box spans the full travel range:
			//   top surface (at rest)   = maxs[2]
			//   bottom surface (lowered) = maxs[2] - travelDist
			// We nudge each candidate 16 units above the surface before
			// querying so findNearestPoly can snap down onto the navmesh.
			const char *heightStr = Nav_ValueForKey( ent, "height" );
			float travelDist = heightStr[0] ? atof( heightStr )
			                               : ( model->maxs[2] - model->mins[2] );

			float cx = ( model->mins[0] + model->maxs[0] ) * 0.5f;
			float cy = ( model->mins[1] + model->maxs[1] ) * 0.5f;

			vec3_t topPt    = { cx, cy, model->maxs[2] + 16.0f };
			vec3_t bottomPt = { cx, cy, model->maxs[2] - travelDist + 16.0f };

			Sys_Printf( "Elevator (model %s): top=%.0f bottom=%.0f travel=%.0f\n",
			            modelStr, model->maxs[2], model->maxs[2] - travelDist, travelDist );

			TransformPointToRecast( topPt );
			TransformPointToRecast( bottomPt );

			// Snap both endpoints to the nearest navmesh polygon.
			// If either misses the navmesh the connection would be silently
			// dropped by dtCreateNavMeshData, so we validate here and log.
			float snapExt[3] = { 80.0f, 160.0f, 80.0f };
			if ( useMeters ) {
				snapExt[0] *= 0.0254f;
				snapExt[1] *= 0.0254f;
				snapExt[2] *= 0.0254f;
			}

			dtPolyRef topRef = 0, bottomRef = 0;
			float topSnapped[3], bottomSnapped[3];

			query->findNearestPoly( topPt,    snapExt, &filter, &topRef,    topSnapped );
			query->findNearestPoly( bottomPt, snapExt, &filter, &bottomRef, bottomSnapped );

			if ( !topRef ) {
				Sys_Printf( "  -> top surface not on navmesh, skipping.\n" );
				continue;
			}
			if ( !bottomRef ) {
				Sys_Printf( "  -> bottom surface not on navmesh, skipping.\n" );
				continue;
			}

			{
				vec3_t gBot, gTop;
				TransformPointToGame( bottomSnapped, gBot );
				TransformPointToGame( topSnapped,    gTop );
				Sys_Printf( "  -> bottom navpt: (%.1f, %.1f, %.1f)\n", gBot[0], gBot[1], gBot[2] );
				Sys_Printf( "  -> top    navpt: (%.1f, %.1f, %.1f)\n", gTop[0], gTop[1], gTop[2] );
				Sys_Printf( "  -> linked (bidirectional off-mesh connection added).\n" );
			}

			offMeshConVerts.push_back( bottomSnapped[0] );
			offMeshConVerts.push_back( bottomSnapped[1] );
			offMeshConVerts.push_back( bottomSnapped[2] );
			offMeshConVerts.push_back( topSnapped[0] );
			offMeshConVerts.push_back( topSnapped[1] );
			offMeshConVerts.push_back( topSnapped[2] );

			float radius = 48.0f;
			if ( useMeters ) radius *= 0.0254f;

			offMeshConRad.push_back( radius );
			offMeshConDir.push_back( 1 ); // bidirectional
			offMeshConAreas.push_back( POLYAREA_ELEVATOR );
			offMeshConFlags.push_back( 1 );
			offMeshConUserID.push_back( 0 );
			elevatorCount++;

			// Store full travel column in Quake space for sidecar start-point nudging.
			{
				ElevatorVolume ev;
				ev.minx = model->mins[0]; ev.maxx = model->maxs[0];
				ev.miny = model->mins[1]; ev.maxy = model->maxs[1];
				ev.minz = model->maxs[2] - travelDist; ev.maxz = model->maxs[2];
				ev.modelStr = modelStr;
				elevatorVolumes.push_back( ev );
				Sys_Printf( "  [EV] func_plat %s volume: X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f]\n",
				            modelStr, ev.minx, ev.maxx, ev.miny, ev.maxy, ev.minz, ev.maxz );
			}
		}
		else if ( !Q_stricmp( classname, "func_door" ) ) {
			// Only handle vertically-moving doors used as lifts/elevators.
			// Q3/JKA convention: angle -1 = moves UP, angle -2 = moves DOWN.
			// Horizontal sliding doors (angle 0-360) are ignored.
			const char *angleStr = Nav_ValueForKey( ent, "angle" );
			int doorAngle = angleStr[0] ? atoi( angleStr ) : 0;
			if ( doorAngle != -1 && doorAngle != -2 ) continue;

			const char *modelStr = Nav_ValueForKey( ent, "model" );
			if ( modelStr[0] != '*' ) continue;

			int modelNum = atoi( &modelStr[1] );
			if ( modelNum < 0 || modelNum >= numBSPModels ) continue;

			bspModel_t *model = &bspModels[modelNum];

			// Travel = door height minus lip (amount that stays visible when open).
			// Default lip in Q3/JKA is 8 units.
			const char *lipStr = Nav_ValueForKey( ent, "lip" );
			float lip      = lipStr[0] ? atof( lipStr ) : 8.0f;
			float doorHeight = model->maxs[2] - model->mins[2];
			float travel   = doorHeight - lip;

			if ( travel < 64.0f ) continue; // not tall enough to be an elevator

			float cx = ( model->mins[0] + model->maxs[0] ) * 0.5f;
			float cy = ( model->mins[1] + model->maxs[1] ) * 0.5f;

			vec3_t topPt, bottomPt;

			if ( doorAngle == -2 ) {
				// Door rests HIGH (closed), drops DOWN when triggered.
				// Top: player on door at rest.  Bottom: player on door when lowered.
				topPt[0]    = cx; topPt[1]    = cy; topPt[2]    = model->maxs[2] + 16.0f;
				bottomPt[0] = cx; bottomPt[1] = cy; bottomPt[2] = model->maxs[2] - travel + 16.0f;
			} else {
				// doorAngle == -1: door rests LOW (closed), rises UP when triggered.
				// Bottom: player on door at rest.  Top: player on door when raised.
				bottomPt[0] = cx; bottomPt[1] = cy; bottomPt[2] = model->maxs[2] + 16.0f;
				topPt[0]    = cx; topPt[1]    = cy; topPt[2]    = model->maxs[2] + travel + 16.0f;
			}

			float topZ    = ( doorAngle == -2 ) ? model->maxs[2]          : model->maxs[2] + travel;
			float bottomZ = ( doorAngle == -2 ) ? model->maxs[2] - travel : model->maxs[2];
			Sys_Printf( "Elevator door (model %s, angle %d): top=%.0f bottom=%.0f travel=%.0f\n",
			            modelStr, doorAngle, topZ, bottomZ, travel );

			TransformPointToRecast( topPt );
			TransformPointToRecast( bottomPt );

			float snapExt[3] = { 80.0f, 160.0f, 80.0f };
			if ( useMeters ) {
				snapExt[0] *= 0.0254f;
				snapExt[1] *= 0.0254f;
				snapExt[2] *= 0.0254f;
			}

			dtPolyRef topRef = 0, bottomRef = 0;
			float topSnapped[3], bottomSnapped[3];

			query->findNearestPoly( topPt,    snapExt, &filter, &topRef,    topSnapped );
			query->findNearestPoly( bottomPt, snapExt, &filter, &bottomRef, bottomSnapped );

			if ( !topRef ) {
				Sys_Printf( "  -> top not on navmesh, skipping.\n" );
				continue;
			}
			if ( !bottomRef ) {
				Sys_Printf( "  -> bottom not on navmesh, skipping.\n" );
				continue;
			}

			{
				vec3_t gBot, gTop;
				TransformPointToGame( bottomSnapped, gBot );
				TransformPointToGame( topSnapped,    gTop );
				Sys_Printf( "  -> bottom navpt: (%.1f, %.1f, %.1f)\n", gBot[0], gBot[1], gBot[2] );
				Sys_Printf( "  -> top    navpt: (%.1f, %.1f, %.1f)\n", gTop[0], gTop[1], gTop[2] );
				Sys_Printf( "  -> linked (bidirectional off-mesh connection added).\n" );
			}

			offMeshConVerts.push_back( bottomSnapped[0] );
			offMeshConVerts.push_back( bottomSnapped[1] );
			offMeshConVerts.push_back( bottomSnapped[2] );
			offMeshConVerts.push_back( topSnapped[0] );
			offMeshConVerts.push_back( topSnapped[1] );
			offMeshConVerts.push_back( topSnapped[2] );

			float radius = 48.0f;
			if ( useMeters ) radius *= 0.0254f;

			offMeshConRad.push_back( radius );
			offMeshConDir.push_back( 1 ); // bidirectional
			offMeshConAreas.push_back( POLYAREA_ELEVATOR );
			offMeshConFlags.push_back( 1 );
			offMeshConUserID.push_back( 0 );
			elevatorCount++;

			// Store full travel column in Quake space for sidecar start-point nudging.
			{
				float topZ    = ( doorAngle == -2 ) ? model->maxs[2]          : model->maxs[2] + travel;
				float bottomZ = ( doorAngle == -2 ) ? model->maxs[2] - travel : model->maxs[2];
				ElevatorVolume ev;
				ev.minx = model->mins[0]; ev.maxx = model->maxs[0];
				ev.miny = model->mins[1]; ev.maxy = model->maxs[1];
				ev.minz = bottomZ; ev.maxz = topZ;
				ev.modelStr = modelStr;
				elevatorVolumes.push_back( ev );
				Sys_Printf( "  [EV] func_door %s volume: X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f]\n",
				            modelStr, ev.minx, ev.maxx, ev.miny, ev.maxy, ev.minz, ev.maxz );
			}
		}
		else if ( !Q_stricmp( classname, "trigger_push" ) ) {
			vec3_t start;
			const char *modelStr = Nav_ValueForKey( ent, "model" );
			if ( modelStr[0] == '*' ) {
				int modelNum = atoi( &modelStr[1] );
				if ( modelNum >= 0 && modelNum < numBSPModels ) {
					bspModel_t *model = &bspModels[modelNum];
					start[0] = (model->mins[0] + model->maxs[0]) * 0.5f;
					start[1] = (model->mins[1] + model->maxs[1]) * 0.5f;
					start[2] = (model->mins[2] + model->maxs[2]) * 0.5f;
				} else continue;
			} else continue;

			Sys_Printf("Found trigger_push (model %s) at %.0f %.0f %.0f\n", modelStr, start[0], start[1], start[2]);

			const char *target = Nav_ValueForKey( ent, "target" );
			const char *speedStr = Nav_ValueForKey( ent, "speed" );
			const char *angleStr = Nav_ValueForKey( ent, "angle" );
			const char *anglesStr = Nav_ValueForKey( ent, "angles" );

			vec3_t recastStart;
			VectorCopy(start, recastStart);
			TransformPointToRecast(recastStart);

			std::vector<LandingPoint> landings;

			if ( target[0] ) {
				entity_t *targetEnt = Nav_FindTargetEntity( target );
				if ( targetEnt ) {
					vec3_t end;
					Nav_GetVectorForKey( targetEnt, "origin", end );
					
					vec3_t recastEnd;
					VectorCopy(end, recastEnd);
					TransformPointToRecast(recastEnd);

					float currentRadius = 64.0f;
					float maxRadius = 512.0f;
					float expansion = 64.0f;

					while (currentRadius <= maxRadius) {
						float rcExtents[3] = { currentRadius, 512.0f, currentRadius };
						if (useMeters) {
							rcExtents[0] *= 0.0254f; rcExtents[1] *= 0.0254f; rcExtents[2] *= 0.0254f;
						}

						dtPolyRef polys[256];
						int polyCount = 0;
						query->queryPolygons(recastEnd, rcExtents, &filter, polys, &polyCount, 256);

						if (polyCount > 0) {
							for (int p = 0; p < polyCount; ++p) {
								float candCenter[3];
								query->closestPointOnPoly(polys[p], recastEnd, candCenter, 0);

								if (candCenter[1] <= recastStart[1] + minHeightJump) continue; 

								if (Nav_CheckLOS(recastEnd, candCenter)) {
									float dir[3];
									dir[0] = candCenter[0] - recastStart[0];
									dir[1] = 0;
									dir[2] = candCenter[2] - recastStart[2];
									float len = sqrt(dir[0]*dir[0] + dir[2]*dir[2]);
									if (len > 0.001f) { dir[0] /= len; dir[2] /= len; }

									bool tooClose = false;
									for (size_t l = 0; l < landings.size(); ++l) {
										float ldir[3];
										ldir[0] = landings[l].pos[0] - recastStart[0];
										ldir[1] = 0;
										ldir[2] = landings[l].pos[2] - recastStart[2];
										float llen = sqrt(ldir[0]*ldir[0] + ldir[2]*ldir[2]);
										if (llen > 0.001f) { ldir[0] /= llen; ldir[2] /= llen; }

										float dot = dir[0]*ldir[0] + dir[2]*ldir[2];
										if (dot > 0.342f) { // cos(70 deg)
											tooClose = true; break;
										}
									}

									if (!tooClose) {
										LandingPoint lp;
										dtVcopy(lp.pos, candCenter);
										landings.push_back(lp);

										offMeshConVerts.push_back(recastStart[0]);
										offMeshConVerts.push_back(recastStart[1]);
										offMeshConVerts.push_back(recastStart[2]);
										offMeshConVerts.push_back(candCenter[0]);
										offMeshConVerts.push_back(candCenter[1]);
										offMeshConVerts.push_back(candCenter[2]);

										float radius = 64.0f;
										if (useMeters) radius *= 0.0254f;
										offMeshConRad.push_back(radius);
										offMeshConDir.push_back(0);
										offMeshConAreas.push_back(POLYAREA_JUMPPAD);
										offMeshConFlags.push_back(1);
										offMeshConUserID.push_back(0);
										jumpPadCount++;
									}
								}
							}
						}
						// If we found ANY landings at this radius, STOP expanding to prevent "Superman" jumps
						if (!landings.empty()) break;
						currentRadius += expansion;
					}
				}
			} else {
				// VARIANT B: Omni-Directional Jump Pad
				bool isVertical = (angleStr[0] == '\0' || !strcmp(angleStr, "-1")) && anglesStr[0] == '\0';

				if (!isVertical) {
					Sys_Printf("  - Horizontal jump pad arc detected; skipping.\n");
				} else if ( speedStr[0] ) {
					float speed = atof(speedStr);
					float height = (speed * speed) / (2.0f * 800.0f);
					float halfHeight = height * 0.5f;

					vec3_t midPt = { start[0], start[1], start[2] + halfHeight };
					vec3_t apexPt = { start[0], start[1], start[2] + height };
					vec3_t recastMid, recastApex;
					
					VectorCopy(midPt, recastMid);
					TransformPointToRecast(recastMid);
					VectorCopy(apexPt, recastApex);
					TransformPointToRecast(recastApex);

					float currentRadius = 250.0f * (speed / 800.0f);
					float maxRadius = currentRadius + 128.0f;
					float expansion = 64.0f;

					while (currentRadius <= maxRadius) {
						float rcExtents[3] = { currentRadius, halfHeight, currentRadius };
						if (useMeters) {
							rcExtents[0] *= 0.0254f; rcExtents[1] *= 0.0254f; rcExtents[2] *= 0.0254f;
						}

						dtPolyRef polys[256];
						int polyCount = 0;
						query->queryPolygons(recastMid, rcExtents, &filter, polys, &polyCount, 256);

						if (polyCount > 0) {
							for (int p = 0; p < polyCount; ++p) {
								float candCenter[3];
								query->closestPointOnPoly(polys[p], recastApex, candCenter, 0);

								if (candCenter[1] <= recastStart[1] + minHeightJump) continue; 

								if (Nav_CheckLOS(recastApex, candCenter)) {
									float dir[3];
									dir[0] = candCenter[0] - recastStart[0];
									dir[1] = 0;
									dir[2] = candCenter[2] - recastStart[2];
									float len = sqrt(dir[0]*dir[0] + dir[2]*dir[2]);
									if (len > 0.001f) { dir[0] /= len; dir[2] /= len; }

									bool tooClose = false;
									for (size_t l = 0; l < landings.size(); ++l) {
										float ldir[3];
										ldir[0] = landings[l].pos[0] - recastStart[0];
										ldir[1] = 0;
										ldir[2] = landings[l].pos[2] - recastStart[2];
										float llen = sqrt(ldir[0]*ldir[0] + ldir[2]*ldir[2]);
										if (llen > 0.001f) { ldir[0] /= llen; ldir[2] /= llen; }

										float dot = dir[0]*ldir[0] + dir[2]*ldir[2];
										if (dot > 0.342f) { // cos(70 deg)
											tooClose = true; break;
										}
									}

									if (!tooClose) {
										LandingPoint lp;
										dtVcopy(lp.pos, candCenter);
										landings.push_back(lp);

										offMeshConVerts.push_back(recastStart[0]);
										offMeshConVerts.push_back(recastStart[1]);
										offMeshConVerts.push_back(recastStart[2]);
										offMeshConVerts.push_back(candCenter[0]);
										offMeshConVerts.push_back(candCenter[1]);
										offMeshConVerts.push_back(candCenter[2]);

										float radius = 64.0f;
										if (useMeters) radius *= 0.0254f;
										offMeshConRad.push_back(radius);
										offMeshConDir.push_back(0);
										offMeshConAreas.push_back(POLYAREA_JUMPPAD);
										offMeshConFlags.push_back(1);
										offMeshConUserID.push_back(0);
										jumpPadCount++;
									}
								}
							}
						}
						// STOP expanding if we found valid points at this Tier
						if (!landings.empty()) break;
						currentRadius += expansion;
					}
				}
			}
		}
	}
	Sys_Printf("Off-mesh connection extraction complete: %d elevators, %d jump pads linked.\n", elevatorCount, jumpPadCount);

	// Auto-detected geometric connections.
	// Boundary edges are shared across all passes — compute once.
	Sys_Printf( "Extracting boundary edges...\n" );
	std::vector<BoundaryEdge> boundaryEdges = Nav_GetBoundaryEdges( mesh );
	Sys_Printf( "  %d boundary edges found.\n", (int)boundaryEdges.size() );

	{
		time_t t0 = time( NULL );
		Sys_Printf( "Running Pass 1 (drop connections)...\n" );
		Nav_DetectDropConnections( query, mesh, boundaryEdges );
		Sys_Printf( "  Pass 1 time: %ds\n", (int)(time(NULL)-t0) );

		t0 = time( NULL );
		Sys_Printf( "Running Pass 2 (basic jump connections)...\n" );
		Nav_DetectJumpConnections( query, mesh, boundaryEdges );
		Sys_Printf( "  Pass 2 time: %ds\n", (int)(time(NULL)-t0) );

		// Pass 3 (auto-detected wallrun connections) is disabled —
		// wallrun connections are supplied via the .nav_connections sidecar file instead.
		// t0 = time( NULL );
		// Sys_Printf( "Running Pass 3 (wallrun ascend connections)...\n" );
		// Nav_DetectWallrunConnections( query, mesh, boundaryEdges );
		// Sys_Printf( "  Pass 3 time: %ds\n", (int)(time(NULL)-t0) );
	}

	// --- Sidecar hand-authored connections ---
	{
		time_t t0 = time( NULL );
		Sys_Printf( "Loading sidecar off-mesh connections...\n" );
		Nav_LoadSidecarConnections( query );
		Sys_Printf( "  Sidecar time: %ds\n", (int)(time(NULL)-t0) );
	}

	// --- Deduplication pass ---
	// If two connections have start AND end both within DEDUP_RADIUS of each other,
	// keep the one with the higher-priority area type.
	// Priority (0 = highest): jumppad=0, elevator=1, wallrun=2, drop=3, jump=4
	// Rationale: jumppads/elevators come from entities (truth), wallruns from the
	// sidecar (hand-authored truth), drops/jumps are computed and less reliable.
	// Runs after all passes so it covers Pass 1/2/3 + sidecar connections.
	auto dedupPriority = []( unsigned char area ) -> int {
		switch ( area ) {
			case POLYAREA_JUMPPAD:      return 0;
			case POLYAREA_ELEVATOR:     return 1;
			case AREA_WALLRUN_ASCEND:   return 2;
			case AREA_JUMP_DROP:        return 3;
			case AREA_JUMP_BASIC:       return 4;
			default:                    return 5;
		}
	};
	//
	// Algorithm: sort connections by start-X, then sweep with a window.  For each i
	// we only compare j where |start_j.x - start_i.x| <= DEDUP_RADIUS — once j's
	// start-X exceeds the window we can break the inner loop.  This gives O(n log n)
	// sort + O(n·k) sweep where k is the average number of connections per 64-unit
	// band, making it fast even for 100K+ connections.
	const float DEDUP_RADIUS = 64.0f * ( useMeters ? 0.0254f : 1.0f );
	const float DEDUP_R2     = DEDUP_RADIUS * DEDUP_RADIUS;

	int nCon = (int)offMeshConVerts.size() / 6;
	if ( nCon > 1 ) {
		// Build a sort-index ordered by start.x
		std::vector<int> order( nCon );
		for ( int i = 0; i < nCon; i++ ) order[i] = i;
		std::sort( order.begin(), order.end(), [&]( int a, int b ) {
			return offMeshConVerts[a * 6] < offMeshConVerts[b * 6];
		} );

		std::vector<bool> keep( nCon, true );

		for ( int ii = 0; ii < nCon; ii++ ) {
			int i = order[ii];
			if ( !keep[i] ) continue;
			const float *si = &offMeshConVerts[i * 6];
			const float *ei = &offMeshConVerts[i * 6 + 3];

			for ( int jj = ii + 1; jj < nCon; jj++ ) {
				int j = order[jj];
				const float *sj = &offMeshConVerts[j * 6];

				// Window check on sorted axis: once start-X gap exceeds radius we're done.
				float dsx1 = sj[0] - si[0];
				if ( dsx1 > DEDUP_RADIUS ) break;

				if ( !keep[j] ) continue;
				const float *ej = &offMeshConVerts[j * 6 + 3];

				float dsx = si[0]-sj[0], dsy = si[1]-sj[1], dsz = si[2]-sj[2];
				float dex = ei[0]-ej[0], dey = ei[1]-ej[1], dez = ei[2]-ej[2];
				if ( dsx*dsx + dsy*dsy + dsz*dsz > DEDUP_R2 ) continue;
				if ( dex*dex + dey*dey + dez*dez > DEDUP_R2 ) continue;

				// Both endpoints close — keep the higher-priority type (lower priority rank).
				if ( dedupPriority( offMeshConAreas[i] ) <= dedupPriority( offMeshConAreas[j] ) ) {
					keep[j] = false;
				} else {
					keep[i] = false;
					break; // i is gone, move to next i
				}
			}
		}

		// Rebuild compacted arrays
		std::vector<float>         newVerts;
		std::vector<float>         newRad;
		std::vector<unsigned char> newDir;
		std::vector<unsigned char> newAreas;
		std::vector<unsigned short>newFlags;
		std::vector<unsigned int>  newIDs;

		for ( int i = 0; i < nCon; i++ ) {
			if ( !keep[i] ) continue;
			const float *v = &offMeshConVerts[i * 6];
			newVerts.insert( newVerts.end(), v, v + 6 );
			newRad.push_back  ( offMeshConRad[i]   );
			newDir.push_back  ( offMeshConDir[i]   );
			newAreas.push_back( offMeshConAreas[i] );
			newFlags.push_back( offMeshConFlags[i] );
			newIDs.push_back  ( offMeshConUserID[i] );
		}

		int nAfter = (int)newVerts.size() / 6;
		Sys_Printf( "Dedup (r=64): %d -> %d connections  (removed %d)\n", nCon, nAfter, nCon - nAfter );

		offMeshConVerts   = std::move( newVerts  );
		offMeshConRad     = std::move( newRad    );
		offMeshConDir     = std::move( newDir    );
		offMeshConAreas   = std::move( newAreas  );
		offMeshConFlags   = std::move( newFlags  );
		offMeshConUserID  = std::move( newIDs    );
	}
}

static void BuildNavMesh( int characterNum ){
	Character agent = characterArray[ characterNum ];

	if ( useMeters ) {
		agent.height *= 0.0254f;
		agent.radius *= 0.0254f;
	}

	dtTileCache *tileCache;
	const float *bmin = geo.getMins();
	const float *bmax = geo.getMaxs();
	int gw = 0, gh = 0;
	const float cellSize = agent.radius / 4.0f;

	rcCalcGridSize( bmin, bmax, cellSize, &gw, &gh );

	const int ts = tileSize;
	const int tw = ( gw + ts - 1 ) / ts;
	const int th = ( gh + ts - 1 ) / ts;

	rcConfig cfg;
	memset( &cfg, 0, sizeof( cfg ) );

	cfg.cs = cellSize;
	cfg.ch = cellHeight;
	cfg.walkableSlopeAngle = RAD2DEG( acosf( MIN_WALK_NORMAL ) );
	cfg.walkableHeight = ( int ) ceilf( agent.height / cfg.ch );
	cfg.walkableClimb = ( int ) floorf( stepSize / cfg.ch );
	cfg.walkableRadius = ( int ) ceilf( agent.radius / cfg.cs );
	// maxEdgeLen: 12 metres in Recast space, regardless of coordinate mode.
	// In -meters mode cfg.cs is already in metres; in Quake-units mode multiply by 0.0254.
	cfg.maxEdgeLen = ( int )( 12.0f / ( cfg.cs * ( useMeters ? 1.0f : 0.0254f ) ) );
	cfg.maxSimplificationError = 1.3f;
	cfg.minRegionArea = ( int )rcSqr( 4.0f );               // voxels
	cfg.mergeRegionArea = ( int )rcSqr( 20.0f );            // voxels
	cfg.maxVertsPerPoly = 6;
	cfg.detailSampleDist = cfg.cs * 6.0f;
	cfg.detailSampleMaxError = cfg.ch * 1.0f;

	rcVcopy( cfg.bmin, bmin );
	rcVcopy( cfg.bmax, bmax );

	if ( useSoloMesh ) {
		Sys_Printf( "Building Solo Mesh...\n" );
		UnvContext context;
		context.enableLog( true );

		// rcCompactCell.index is a 24-bit field — max value 16,777,215.
		// Maps with very dense geometry can exceed this, causing an out-of-bounds
		// memory write in rcBuildCompactHeightfield / rcErodeWalkableAreaByBox
		// (Windows exit code 0xC0000005 / segfault on Linux).
		// Fix: retry with progressively coarser cell sizes until span count fits.
		const int RC_COMPACT_INDEX_MAX = (1 << 24) - 1; // 16,777,215

		const float *verts = geo.getVerts();
		const int nverts = geo.getNumVerts();
		const rcChunkyTriMesh *chunkyMesh = geo.getChunkyMesh();
		unsigned char* triareas = new unsigned char[ chunkyMesh->maxTrisPerChunk ];

		rcHeightfield* solid = nullptr;
		rcCompactHeightfield* chf = nullptr;

		for ( int csAttempt = 0; csAttempt < 5; csAttempt++ ) {
			if ( csAttempt > 0 ) {
				// Double the cell size and recompute all dependent config values
				cfg.cs *= 2.0f;
				cfg.walkableRadius = (int)ceilf( agent.radius / cfg.cs );
				cfg.maxEdgeLen = (int)( 12.0f / ( cfg.cs * ( useMeters ? 1.0f : 0.0254f ) ) );
				rcCalcGridSize( bmin, bmax, cfg.cs, &gw, &gh );
				Sys_Printf( "  Retrying with cs=%.5f (grid %dx%d)\n", cfg.cs, gw, gh );
			}

			rcFreeHeightField( solid );
			rcFreeCompactHeightfield( chf );

			solid = rcAllocHeightfield();
			if ( !rcCreateHeightfield( &context, *solid, gw, gh, bmin, bmax, cfg.cs, cfg.ch ) ) {
				Error( "Could not create heightfield\n" );
			}

			for ( int i = 0; i < chunkyMesh->nnodes; i++ ) {
				const rcChunkyTriMeshNode &node = chunkyMesh->nodes[ i ];
				if ( node.i < 0 ) continue;
				const int *tris = &chunkyMesh->tris[ node.i * 3 ];
				const int ntris = node.n;

				memset( triareas, 0, ntris * sizeof( unsigned char ) );
				rcMarkWalkableTriangles( &context, cfg.walkableSlopeAngle, verts, nverts, tris, ntris, triareas );
				rcRasterizeTriangles( &context, verts, nverts, tris, triareas, ntris, *solid, cfg.walkableClimb );
			}

			rcFilterLowHangingWalkableObstacles( &context, cfg.walkableClimb, *solid );
			rcFilterWalkableLowHeightSpans( &context, cfg.walkableHeight, *solid );

			if ( filterGaps ) {
				rcFilterGaps( &context, cfg.walkableRadius, cfg.walkableClimb, cfg.walkableHeight, *solid );
			}

			// Count walkable spans before compact heightfield
			{
				int walkableSpans = 0;
				for ( int i = 0; i < solid->width * solid->height; i++ ) {
					for ( rcSpan *s = solid->spans[i]; s; s = s->next ) {
						if ( s->area != RC_NULL_AREA ) walkableSpans++;
					}
				}
				Sys_Printf( "  Walkable spans after filter: %d  (grid %dx%d)\n",
				            walkableSpans, solid->width, solid->height );
			}

			chf = rcAllocCompactHeightfield();
			if ( !rcBuildCompactHeightfield( &context, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf ) ) {
				Error( "Could not build compact heightfield\n" );
			}

			// Count walkable spans in compact HF
			{
				int walkable = 0;
				for ( int i = 0; i < chf->spanCount; i++ )
					if ( chf->areas[i] != RC_NULL_AREA ) walkable++;
				Sys_Printf( "  Compact HF: %d spans, %d walkable\n", chf->spanCount, walkable );
			}

			if ( chf->spanCount <= RC_COMPACT_INDEX_MAX ) break;

			Sys_Printf( "  WARNING: span count %d exceeds 24-bit index limit (%d), doubling cell size\n",
			            chf->spanCount, RC_COMPACT_INDEX_MAX );
		}

		if ( chf->spanCount > RC_COMPACT_INDEX_MAX ) {
			Error( "Could not reduce span count below 24-bit index limit after 5 attempts\n" );
		}

		if ( !rcErodeWalkableAreaByBox( &context, cfg.walkableRadius, *chf ) ) {
			Error( "Could not erode walkable area\n" );
		}
		{
			int walkable = 0;
			for ( int i = 0; i < chf->spanCount; i++ )
				if ( chf->areas[i] != RC_NULL_AREA ) walkable++;
			Sys_Printf( "  After erosion (r=%d vox): %d walkable spans remain\n", cfg.walkableRadius, walkable );
		}

		if ( !rcBuildDistanceField( &context, *chf ) ) {
			Error( "Could not build distance field\n" );
		}

		if ( !rcBuildRegions( &context, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea ) ) {
			Error( "Could not build regions\n" );
		}
		Sys_Printf( "  Regions built: maxRegions=%d maxDist=%d\n", (int)chf->maxRegions, (int)chf->maxDistance );

		// If distance field saturated and region building produced nothing useful,
		// fall back to monotone region building which doesn't rely on distance field.
		if ( chf->maxDistance == 0xFFFF && chf->maxRegions <= 1 ) {
			Sys_Printf( "  WARNING: distance field saturated (isolated spans?), retrying with monotone region building\n" );
			// Reset all region IDs
			for ( int i = 0; i < chf->spanCount; i++ )
				chf->spans[i].reg = 0;
			chf->maxRegions = 0;
			if ( !rcBuildRegionsMonotone( &context, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea ) ) {
				Error( "Could not build monotone regions\n" );
			}
			Sys_Printf( "  Monotone regions: maxRegions=%d\n", (int)chf->maxRegions );
		}

		// Build contours, retrying with progressively larger minRegionArea if the total
		// vertex count would exceed rcBuildPolyMesh's limit (~0x7FFF verts).
		// Very large maps using the monotone fallback can produce 40K+ contours; the fix
		// is to cull more small regions rather than to simplify individual contours.
		const int RC_POLYMESH_MAX_VERTS = 0x7FFF; // 32767
		rcContourSet* cset = rcAllocContourSet();
		int regionAreaMultiplier = 1;
		for ( int attempt = 0; attempt < 5; attempt++ ) {
			rcFreeContourSet( cset );
			cset = rcAllocContourSet();
			if ( !rcBuildContours( &context, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset ) ) {
				Error( "Could not build contours\n" );
			}
			// Count total contour vertices
			int totalVerts = 0;
			for ( int ci = 0; ci < cset->nconts; ci++ )
				totalVerts += cset->conts[ci].nverts;
			Sys_Printf( "  Contours: %d  total verts: %d  (minRegionArea=%d)\n",
			            cset->nconts, totalVerts, cfg.minRegionArea * regionAreaMultiplier );
			if ( totalVerts < RC_POLYMESH_MAX_VERTS ) break;

			// Too many verts — cull more small regions and rebuild
			regionAreaMultiplier *= 8;
			int newMinArea = cfg.minRegionArea * regionAreaMultiplier;
			Sys_Printf( "  WARNING: too many contour verts (%d), rebuilding regions with minRegionArea=%d\n",
			            totalVerts, newMinArea );
			// Reset region IDs and rebuild monotone regions with higher threshold
			for ( int i = 0; i < chf->spanCount; i++ )
				chf->spans[i].reg = 0;
			chf->maxRegions = 0;
			if ( !rcBuildRegionsMonotone( &context, *chf, 0, newMinArea, cfg.mergeRegionArea * regionAreaMultiplier ) ) {
				Error( "Could not rebuild monotone regions\n" );
			}
			Sys_Printf( "  Rebuilt regions: maxRegions=%d\n", (int)chf->maxRegions );
		}

		rcPolyMesh* pmesh = rcAllocPolyMesh();
		if ( !rcBuildPolyMesh( &context, *cset, cfg.maxVertsPerPoly, *pmesh ) ) {
			Error( "Could not build polymesh\n" );
		}

		rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
		if ( !rcBuildPolyMeshDetail( &context, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh ) ) {
			Error( "Could not build polymesh detail\n" );
		}

		for (int i = 0; i < pmesh->npolys; ++i) {
			pmesh->flags[i] = 1; // Default Walkable
		}

		unsigned char* navData = 0;
		int navDataSize = 0;

		dtNavMeshCreateParams params;
		memset( &params, 0, sizeof( params ) );
		params.verts = pmesh->verts;
		params.vertCount = pmesh->nverts;
		params.polys = pmesh->polys;
		params.polyAreas = pmesh->areas;
		params.polyFlags = pmesh->flags;
		params.polyCount = pmesh->npolys;
		params.nvp = pmesh->nvp;
		params.detailMeshes = dmesh->meshes;
		params.detailVerts = dmesh->verts;
		params.detailVertsCount = dmesh->nverts;
		params.detailTris = dmesh->tris;
		params.detailTriCount = dmesh->ntris;
		params.walkableHeight = agent.height;
		params.walkableRadius = agent.radius;
		params.walkableClimb = stepSize;
		rcVcopy( params.bmin, pmesh->bmin );
		rcVcopy( params.bmax, pmesh->bmax );
		params.cs = cfg.cs;
		params.ch = cfg.ch;
		params.buildBvTree = true;

		Sys_Printf("PolyMesh Poly Count: %d\n", pmesh->npolys);
		if ( !dtCreateNavMeshData( &params, &navData, &navDataSize ) ) {
			Error( "Could not build Detour navmesh data\n" );
		}

		dtNavMesh* tempMesh = dtAllocNavMesh();
		tempMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
		dtNavMeshQuery* tempQuery = dtAllocNavMeshQuery();
		tempQuery->init(tempMesh, 2048);

		ExtractOffMeshConnections(tempQuery, tempMesh);

		params.offMeshConCount = offMeshConRad.size();
		params.offMeshConVerts = offMeshConVerts.data();
		params.offMeshConRad = offMeshConRad.data();
		params.offMeshConDir = offMeshConDir.data();
		params.offMeshConAreas = offMeshConAreas.data();
		params.offMeshConFlags = offMeshConFlags.data();
		params.offMeshConUserID = offMeshConUserID.data();

		unsigned char* finalNavData = 0;
		int finalNavDataSize = 0;

		if ( !dtCreateNavMeshData( &params, &finalNavData, &finalNavDataSize ) ) {
			Error( "Could not build final Detour navmesh data\n" );
		}

		WriteSoloNavMeshFile( finalNavData, finalNavDataSize, cfg );

		dtFree( finalNavData );
		dtFreeNavMeshQuery(tempQuery);
		dtFreeNavMesh(tempMesh);
		rcFreePolyMeshDetail( dmesh );
		rcFreePolyMesh( pmesh );
		rcFreeContourSet( cset );
		rcFreeCompactHeightfield( chf );
		rcFreeHeightField( solid );
		delete[] triareas;
		return;
	}

	dtTileCacheParams tcparams;
	memset( &tcparams, 0, sizeof( tcparams ) );
	rcVcopy( tcparams.orig, bmin );
	tcparams.cs = cellSize;
	tcparams.ch = cellHeight;
	tcparams.width = ts;
	tcparams.height = ts;
	tcparams.walkableHeight = agent.height;
	tcparams.walkableRadius = agent.radius;
	tcparams.walkableClimb = stepSize;
	tcparams.maxSimplificationError = 1.3;
	tcparams.maxTiles = tw * th * EXPECTED_LAYERS_PER_TILE;
	tcparams.maxObstacles = 256;

	tileCache = dtAllocTileCache();

	if ( !tileCache ) {
		Error( "Could not allocate tile cache\n" );
	}

	LinearAllocator alloc( 32000 );
	FastLZCompressor comp;
	MeshProcess proc;

	dtStatus status = tileCache->init( &tcparams, &alloc, &comp, &proc );

	if ( dtStatusFailed( status ) ) {
		if ( dtStatusDetail( status, DT_INVALID_PARAM ) ) {
			Error( "Could not init tile cache: Invalid parameter\n" );
		}
		else
		{
			Error( "Could not init tile cache\n" );
		}
	}

	UnvContext context;
	context.enableLog( true );

	//iterate over all tiles (number is determined by rcCalcGridSize)
	for ( int y = 0; y < th; y++ )
	{
		for ( int x = 0; x < tw; x++ )
		{
			TileCacheData tiles[ MAX_LAYERS ];
			memset( tiles, 0, sizeof( tiles ) );

			int ntiles = rasterizeTileLayers( context, x, y, cfg, tiles, MAX_LAYERS );

			for ( int i = 0; i < ntiles; i++ )
			{
				TileCacheData *tile = &tiles[ i ];
				status = tileCache->addTile( tile->data, tile->dataSize, DT_COMPRESSEDTILE_FREE_DATA, 0 );
				if ( dtStatusFailed( status ) ) {
					dtFree( tile->data );
					tile->data = 0;
					continue;
				}
			}
		}
	}

	// there are 22 bits to store a tile and its polys
	int tileBits = rcMin( ( int ) dtIlog2( dtNextPow2( tcparams.maxTiles ) ), 14 );
	int polyBits = 22 - tileBits;

	dtNavMeshParams params;
	dtVcopy( params.orig, tcparams.orig );
	params.tileHeight = ts * cfg.cs;
	params.tileWidth = ts * cfg.cs;
	params.maxTiles = 1 << tileBits;
	params.maxPolys = 1 << polyBits;

	WriteNavMeshFile( agent.name, tileCache, &params );
	dtFreeTileCache( tileCache );
}

/*
   ===========
   NavMain
   ===========
 */
extern "C" int NavMain( int argc, char **argv ){
	float temp;
	int i;

	if ( argc < 2 ) {
		Sys_Printf( "Usage: daemonmap -nav [-cellheight f] [-stepsize f] [-includecaulk] [-includesky] [-nogapfilter] [-solomesh] [-meters] <filename.bsp>\n" );
	Sys_Printf( "  Defaults: -game ja -solomesh -meters (no need to pass these explicitly)\n" );
		return 0;
	}

	/* note it */
	Sys_Printf( "--- Nav ---\n" );

	/* process arguments */
	for ( i = 1; i < ( argc - 1 ); i++ )
	{
		if ( !Q_stricmp( argv[i],"-cellheight" ) ) {
			i++;
			if ( i < ( argc - 1 ) ) {
				temp = atof( argv[i] );
				if ( temp > 0 ) {
					cellHeight = temp;
				}
			}
		}
		else if ( !Q_stricmp( argv[i], "-stepsize" ) ) {
			i++;
			if ( i < ( argc - 1 ) ) {
				temp = atof( argv[i] );
				if ( temp > 0 ) {
					stepSize = temp;
				}
			}
		}
		else if ( !Q_stricmp( argv[i], "-includecaulk" ) ) {
			excludeCaulk = qfalse;
		}
		else if ( !Q_stricmp( argv[i], "-includesky" ) ) {
			excludeSky = qfalse;
		}
		else if ( !Q_stricmp( argv[i], "-nogapfilter" ) ) {
			filterGaps = qfalse;
		}
		else if ( !Q_stricmp( argv[i], "-meters" ) ) {
			useMeters = qtrue;
		}
		else if ( !Q_stricmp( argv[i], "-solomesh" ) ) {
			useSoloMesh = qtrue; // default; kept as explicit flag for back-compat
		}
		else {
			Sys_Printf( "WARNING: Unknown option \"%s\"\n", argv[i] );
		}
	}

	if ( useMeters ) {
		cellHeight *= 0.0254f;
		stepSize *= 0.0254f;
	}

	/* load the bsp */
	sprintf( source, "%s%s", inbase, ExpandArg( argv[i] ) );
	StripExtension( source );
	strcat( source, ".bsp" );
	//LoadShaderInfo();

	Sys_Printf( "Loading %s\n", source );

	LoadBSPFile( source );

	ParseEntities();

	LoadGeometry();

	float height = rcAbs( geo.getMaxs()[1] ) + rcAbs( geo.getMins()[1] );
	if ( height / cellHeight > RC_SPAN_MAX_HEIGHT ) {
		Sys_Printf( "WARNING: Map geometry is too tall for specified cell height. Increasing cell height to compensate. This may cause a less accurate navmesh.\n" );
		Sys_Printf( "Previous cell height: %f\n", cellHeight );

		cellHeight = height / RC_SPAN_MAX_HEIGHT;
	}

	Sys_Printf( "cell height: %f\n", cellHeight );
	Sys_Printf( "step size: %f\n", stepSize );

	if ( cellHeight > stepSize )
	{
		Error( "ERROR: Map is too tall to generate a navigation mesh, cell height can't be greater than step size\n" );
	}

	RunThreadsOnIndividual( sizeof( characterArray ) / sizeof( characterArray[ 0 ] ), qtrue, BuildNavMesh );

	return 0;
}

extern "C" int ObjMain( int argc, char **argv ){
	Sys_Printf("Obj export not supported in this build.\n");
	return 0;
}