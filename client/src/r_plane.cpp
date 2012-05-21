// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id: r_plane.cpp 3174 2012-05-11 01:03:43Z mike $
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 2006-2012 by The Odamex Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Here is a core component: drawing the floors and ceilings,
//	while maintaining a per column clipping list only.
//	Moreover, the sky areas have to be determined.
//
//		MAXVISPLANES is no longer a limit on the number of visplanes,
//		but a limit on the number of hash slots; larger numbers mean
//		better performance usually but after a point they are wasted,
//		and memory and time overheads creep in.
//
//													-Lee Killough
//
//-----------------------------------------------------------------------------


#include <stdio.h>
#include <stdlib.h>

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "doomdef.h"
#include "doomstat.h"

#include "p_local.h"
#include "r_local.h"
#include "r_sky.h"

#include "m_alloc.h"
#include "v_video.h"

#include "vectors.h"
#include <math.h>

planefunction_t 		floorfunc;
planefunction_t 		ceilingfunc;

// Here comes the obnoxious "visplane".
#define MAXVISPLANES 128    /* must be a power of 2 */

static const float flatwidth = 64.f;
static const float flatheight = 64.f;

static visplane_t		*visplanes[MAXVISPLANES];	// killough
static visplane_t		*freetail;					// killough
static visplane_t		**freehead = &freetail;		// killough

visplane_t 				*floorplane;
visplane_t 				*ceilingplane;

// killough -- hash function for visplanes
// Empirically verified to be fairly uniform:

#define visplane_hash(picnum,lightlevel,secplane) \
  ((unsigned)((picnum)*3+(lightlevel)+(secplane.d)*7) & (MAXVISPLANES-1))

//
// opening
//

size_t					maxopenings;
int	    				*openings;
int 					*lastopening;


//
// Clip values are the solid pixel bounding the range.
//	floorclip starts out SCREENHEIGHT
//	ceilingclip starts out -1
//
int     				*floorclip;
int 					*ceilingclip;

//
// spanstart holds the start of a plane span
// initialized to 0 at start
//
int 					*spanstart;

//
// texture mapping
//
extern fixed_t FocalLengthX, FocalLengthY;

int*					planezlight;
fixed_t 				planeheight;
float					plight, shade;

fixed_t 				*yslope;
fixed_t 				*distscale;
fixed_t 				xstepscale;
fixed_t 				ystepscale;
static fixed_t			xscale, yscale;
static fixed_t			pviewx, pviewy;
static angle_t			baseangle;

v3double_t				a, b, c;
float					ixscale, iyscale;

EXTERN_CVAR (r_skypalette)

#ifdef USEASM
extern "C" void R_SetSpanSource_ASM (byte *flat);
extern "C" void R_SetSpanColormap_ASM (byte *colormap);
extern "C" byte *ds_curcolormap, *ds_cursource;
#endif

//
// R_InitPlanes
// Only at game startup.
//
void R_InitPlanes (void)
{
	// Doh!
}

//
// R_MapSlopedPlane
//
// Calculates the vectors a, b, & c, which are used to texture map a sloped
// plane.
//
// Based in part on R_MapSlope() and R_SlopeLights() from Eternity Engine,
// written by SoM/Quasar
//
void R_MapSlopedPlane(int y, int x1, int x2)
{
	int len = x2 - x1 + 1;
	if (len <= 0)
		return;

	// center of the view plane
	v3double_t s;		
	M_SetVec3(&s, double(x1 - centerx),	double(y - centery + 1.0),
				  double(FocalLengthX) / FRACUNIT);

	ds_iu = M_DotProductVec3(&s, &a) * flatwidth;
	ds_iv = M_DotProductVec3(&s, &b) * flatheight;
	ds_id = M_DotProductVec3(&s, &c);
	
	ds_iustep = a.x * flatwidth;
	ds_ivstep = b.x * flatheight;
	ds_idstep = c.x;

	// From R_SlopeLights, Eternity Engine
	double map1, map2;
	map1 = 256.0f - (shade - plight * ds_id);
	if (len > 1)
	{
		double id = ds_id + ds_idstep * (x2 - x1);
		map2 = 256.0f - (shade - plight * id);
	}
	else
		map2 = map1;

	if (fixedlightlev)
		for (int i = 0; i < len; i++)
			slopelighting[i] = basecolormap + fixedlightlev;
	else if (fixedcolormap)
		for (int i = 0; i < len; i++)
			slopelighting[i] = fixedcolormap;
	else
	{
		fixed_t mapstart = FLOAT2FIXED((256.0f - map1) / 256.0f * NUMCOLORMAPS);
		fixed_t mapend = FLOAT2FIXED((256.0f - map2) / 256.0f * NUMCOLORMAPS);
		fixed_t map = mapstart;
		fixed_t step = 0;

		if (len > 1)
			step = (mapend - mapstart) / (len - 1);

		for (int i = 0; i < len; i++)
		{
			int index = (int)(map >> FRACBITS) + 1;
			index -= (foggy ? 0 : extralight << 2);
			
			if (index < 0)
				slopelighting[i] = basecolormap;
			else if (index >= NUMCOLORMAPS)
				slopelighting[i] = basecolormap + 256 * (NUMCOLORMAPS - 1);
			else
				slopelighting[i] = basecolormap + 256 * index;
			
			map += step;
		}
	}

   	ds_y = y;
	ds_x1 = x1;
	ds_x2 = x2;

	spanslopefunc();
}


//
// R_MapLevelPlane
//
// Uses global vars:
//  planeheight
//  ds_source
//  basexscale
//  baseyscale
//  viewx
//  viewy
//
// BASIC PRIMITIVE
//
void
R_MapLevelPlane
( int		y,
  int		x1,
  int		x2 )
{
    angle_t	angle;
    fixed_t	distance;
    fixed_t	length;
    unsigned	index;

#ifdef RANGECHECK
	if (x2 < x1 || x1<0 || x2>=viewwidth || (unsigned)y>=(unsigned)viewheight)
	{
		I_FatalError ("R_MapLevelPlane: %i, %i at %i", x1, x2, y);
	}
#endif

	// Find the z-coordinate of the left edge of the span in camera space
	// This is some simple triangle scaling:
	//		(vertical distance of plane from camera) * (focal length)
	//		---------------------------------------------------------
	//		     (vertical distance of span from screen center)
	distance = FixedMul (planeheight, yslope[y]);

	// Use this to determine stepping values. Because the plane is always
	// viewed with constant z, knowing the distance from the span is enough
	// to do a rough approximation of the stepping values. In reality, you
	// should find the (u,v) coordinates at the left and right edges of the
	// span and step between them, but that involves more math (including
	// some divides).
	ds_xstep = FixedMul (xstepscale, distance) << 10;
	ds_ystep = FixedMul (ystepscale, distance) << 10;

	// Find the length of a 2D vector from the camera to the left edge of
	// the span in camera space. This is accomplished using some trig:
	//		    (distance)
	//		------------------
	//		 sin (view angle)
	length = FixedMul (distance, distscale[x1]);

	// Find the angle from the center of the screen to the start of the span.
	// This is also precalculated in the distscale array used above (minus the
	// baseangle rotation). Baseangle compensates for the player's view angle
	// and also for the texture's rotation relative to the world.
	angle = (baseangle + xtoviewangle[x1]) >> ANGLETOFINESHIFT;

	// Find the (u,v) coordinate of the left edge of the span by extending a
	// ray from the camera position out into texture space. (For all intents and
	// purposes, texture space is equivalent to world space here.) The (u,v) values
	// are multiplied by scaling factors for the plane to scale the texture.
	ds_xfrac = FixedMul (xscale, pviewx + FixedMul (finecosine[angle], length)) << 10;
	ds_yfrac = FixedMul (yscale, pviewy - FixedMul (finesine[angle], length)) << 10;

	if (fixedlightlev)
		ds_colormap = basecolormap + fixedlightlev;
	else if (fixedcolormap)
		ds_colormap = fixedcolormap;
	else
	{
		// Determine lighting based on the span's distance from the viewer.
		index = distance >> LIGHTZSHIFT;

		if (index >= MAXLIGHTZ)
			index = MAXLIGHTZ-1;

		ds_colormap = planezlight[index] + basecolormap;
	}

#ifdef USEASM
	if (ds_colormap != ds_curcolormap)
		R_SetSpanColormap_ASM (ds_colormap);
#endif

	ds_y = y;
	ds_x1 = x1;
	ds_x2 = x2;

	spanfunc ();
}

//
// R_ClearPlanes
// At begining of frame.
//
void R_ClearPlanes (void)
{
	int i;

	// opening / clipping determination
	for (i = 0; i < viewwidth ; i++)
	{
		floorclip[i] = (int)viewheight;
	}
	memset (ceilingclip, 0xffffffffu, sizeof(*ceilingclip) * viewwidth);

	for (i = 0; i < MAXVISPLANES; i++)	// new code -- killough
		for (*freehead = visplanes[i], visplanes[i] = NULL; *freehead; )
			freehead = &(*freehead)->next;

	lastopening = openings;
}

//
// New function, by Lee Killough
// [RH] top and bottom buffers get allocated immediately
//		after the visplane.
//
static visplane_t *new_visplane(unsigned hash)
{
	visplane_t *check = freetail;

	if (!check)
	{
		check = (visplane_t *)Calloc (1, sizeof(*check) + sizeof(*check->top)*(screen->width*2));
		check->bottom = &check->top[screen->width+2];
	}
	else
		if (!(freetail = freetail->next))
			freehead = &freetail;
	check->next = visplanes[hash];
	visplanes[hash] = check;
	return check;
}


//
// R_FindPlane
//
// killough 2/28/98: Add offsets
//
visplane_t *R_FindPlane (plane_t secplane, int picnum, int lightlevel,
						 fixed_t xoffs, fixed_t yoffs,
						 fixed_t xscale, fixed_t yscale, angle_t angle)
{
	visplane_t *check;
	unsigned hash;						// killough

	if (picnum == skyflatnum || picnum & PL_SKYFLAT)  // killough 10/98
		lightlevel = 0;		// most skies map together

	// New visplane algorithm uses hash table -- killough
	hash = visplane_hash (picnum, lightlevel, secplane);

	for (check = visplanes[hash]; check; check = check->next)	// killough
		if (P_IdenticalPlanes(&secplane, &check->secplane) &&
			picnum == check->picnum &&
			lightlevel == check->lightlevel &&
			xoffs == check->xoffs &&	// killough 2/28/98: Add offset checks
			yoffs == check->yoffs &&
			basecolormap == check->colormap &&	// [RH] Add colormap check
			xscale == check->xscale &&
			yscale == check->yscale &&
			angle == check->angle
			)
		  return check;

	check = new_visplane (hash);		// killough

	memcpy(&check->secplane, &secplane, sizeof(secplane));
	check->picnum = picnum;
	check->lightlevel = lightlevel;
	check->xoffs = xoffs;				// killough 2/28/98: Save offsets
	check->yoffs = yoffs;
	check->xscale = xscale;
	check->yscale = yscale;
	check->angle = angle;
	check->colormap = basecolormap;		// [RH] Save colormap
	check->minx = viewwidth;			// Was SCREENWIDTH -- killough 11/98
	check->maxx = -1;

	memset (check->top, 0xff, sizeof(*check->top) * screen->width);

	return check;
}

//
// R_CheckPlane
//
visplane_t*
R_CheckPlane
( visplane_t*	pl,
  int		start,
  int		stop )
{
    int		intrl;
    int		intrh;
    int		unionl;
    int		unionh;
    int		x;

	if (start < pl->minx)
	{
		intrl = pl->minx;
		unionl = start;
	}
	else
	{
		unionl = pl->minx;
		intrl = start;
	}

	if (stop > pl->maxx)
	{
		intrh = pl->maxx;
		unionh = stop;
	}
	else
	{
		unionh = pl->maxx;
		intrh = stop;
	}

	for (x=intrl ; x <= intrh && pl->top[x] == 0xffffffffu; x++)
		;

	if (x > intrh)
	{
		// use the same visplane
		pl->minx = unionl;
		pl->maxx = unionh;
	}
	else
	{
		// make a new visplane
		unsigned hash = visplane_hash (pl->picnum, pl->lightlevel, pl->secplane);
		visplane_t *new_pl = new_visplane (hash);

		new_pl->secplane = pl->secplane;
		new_pl->picnum = pl->picnum;
		new_pl->lightlevel = pl->lightlevel;
		new_pl->xoffs = pl->xoffs;			// killough 2/28/98
		new_pl->yoffs = pl->yoffs;
		new_pl->xscale = pl->xscale;
		new_pl->yscale = pl->yscale;
		new_pl->angle = pl->angle;
		new_pl->colormap = pl->colormap;	// [RH] Copy colormap
		pl = new_pl;
		pl->minx = start;
		pl->maxx = stop;
		memset (pl->top, 0xff, sizeof(*pl->top) * screen->width);
	}
	return pl;
}

//
// [RH] R_DrawSky
//
// Can handle parallax skies. Note that the front sky is *not* masked in
// in the normal convention for patches, but uses color 0 as a transparent
// color.
// [ML] 5/11/06 - Removed sky2

static visplane_t *_skypl;
static int skytex;
static angle_t skyflip;
static int frontpos;

static void _skycolumn (void (*drawfunc)(void), int x)
{
	dc_yl = _skypl->top[x];
	dc_yh = _skypl->bottom[x];

	if (dc_yl != -1 && dc_yl <= dc_yh)
	{
		int angle = ((((viewangle + xtoviewangle[x])^skyflip)>>sky1shift) + frontpos)>>16;

		dc_texturefrac = dc_texturemid + (dc_yl - centery + 1) * dc_iscale;
		dc_source = R_GetColumn (skytex, angle);
		drawfunc ();
	}
}

static void R_DrawSky (visplane_t *pl)
{
	int x;

	if (pl->minx > pl->maxx)
		return;

	dc_mask = 255;
	dc_iscale = skyiscale >> skystretch;
	dc_texturemid = skytexturemid;
	_skypl = pl;

	if (!r_columnmethod)
	{
		for (x = pl->minx; x <= pl->maxx; x++)
		{
			dc_x = x;
			_skycolumn (colfunc, x);
		}
	}
	else
	{
		int stop = (pl->maxx+1) & ~3;

		x = pl->minx;

		if (x & 1)
		{
			dc_x = x;
			_skycolumn (colfunc, x);
			x++;
		}

		if (x & 2)
		{
			if (x < pl->maxx)
			{
				rt_initcols();
				dc_x = 0;
				_skycolumn (hcolfunc_pre, x);
				x++;
				dc_x = 1;
				_skycolumn (hcolfunc_pre, x);
				rt_draw2cols (0, x - 1);
				x++;
			}
			else if (x == pl->maxx)
			{
				dc_x = x;
				_skycolumn (colfunc, x);
				x++;
			}
		}

		while (x < stop)
		{
			rt_initcols();
			dc_x = 0;
			_skycolumn (hcolfunc_pre, x);
			x++;
			dc_x = 1;
			_skycolumn (hcolfunc_pre, x);
			x++;
			dc_x = 2;
			_skycolumn (hcolfunc_pre, x);
			x++;
			dc_x = 3;
			_skycolumn (hcolfunc_pre, x);
			rt_draw4cols (x - 3);
			x++;
		}

		if (pl->maxx == x)
		{
			dc_x = x;
			_skycolumn (colfunc, x);
			x++;
		}
		else if (pl->maxx > x)
		{
			rt_initcols();
			dc_x = 0;
			_skycolumn (hcolfunc_pre, x);
			x++;
			dc_x = 1;
			_skycolumn (hcolfunc_pre, x);
			rt_draw2cols (0, x - 1);
			if (++x <= pl->maxx)
			{
				dc_x = x;
				_skycolumn (colfunc, x);
				x++;
			}
		}
	}
}

//
// R_DrawSlopedPlane
//
// Calculates the vectors a, b, & c, which are used to texture map a sloped
// plane.
//
// Based in part on R_CalcSlope() from Eternity Engine, written by SoM.
//
void R_DrawSlopedPlane(visplane_t *pl)
{
	float sinang = FIXED2FLOAT(finesine[(pl->angle + ANG90) >> ANGLETOFINESHIFT]);
	float cosang = FIXED2FLOAT(finecosine[(pl->angle + ANG90) >> ANGLETOFINESHIFT]);
	
	float xoffsf = FIXED2FLOAT(pl->xoffs);
	float yoffsf = FIXED2FLOAT(pl->yoffs);

	// Scale the flat's texture
	float scaledflatwidth = flatwidth * FIXED2FLOAT(pl->xscale);
	float scaledflatheight = flatheight * FIXED2FLOAT(pl->yscale);
	
	v3double_t p, t, s, m, n;
	
	// Point p is the anchor point of the texture.  It starts out as the
	// map coordinate (0, 0, planez(0,0)) but texture offset and rotation get applied
	p.x = -yoffsf * cosang - xoffsf * sinang;
	p.z = -yoffsf * sinang + xoffsf * cosang;
	p.y = FIXED2FLOAT(P_PlaneZ(FLOAT2FIXED(p.x), FLOAT2FIXED(p.z), &pl->secplane));

	// Point t is the point along the plane (texwidth, 0, planez(texwidth, 0)) with texture
	// offset and rotation applied
	t.x = p.x - scaledflatwidth * sinang;
	t.z = p.z + scaledflatwidth * cosang;
	t.y = FIXED2FLOAT(P_PlaneZ(FLOAT2FIXED(t.x), FLOAT2FIXED(t.z), &pl->secplane));

	// Point s is the point along the plane (0, texheight, planez(0, texheight)) with texture
	// offset and rotation applied
	s.x = p.x + scaledflatheight * cosang;
	s.z = p.z + scaledflatheight * sinang;
	s.y = FIXED2FLOAT(P_PlaneZ(FLOAT2FIXED(s.x), FLOAT2FIXED(s.z), &pl->secplane));
	
	// Translate the points to their position relative to viewx, viewy and
	// rotate them based on viewangle
	M_TranslateVec3(&p);
	M_TranslateVec3(&t);
	M_TranslateVec3(&s);
	
	// Create direction vector m from point p to point t, and n from point p to point s
	M_SubVec3(&m, &t, &p);
	M_SubVec3(&n, &s, &p);
	
	M_CrossProductVec3(&a, &p, &n);
	M_CrossProductVec3(&b, &m, &p);
	M_CrossProductVec3(&c, &m, &n);
	
	double invfocratio =  double(FocalLengthX) / double(FocalLengthY);

	M_ScaleVec3(&a, &a, 0.5);
	M_ScaleVec3(&b, &b, 0.5);
	M_ScaleVec3(&c, &c, 0.5);
	
	a.y *= invfocratio;
	b.y *= invfocratio;
	c.y *= invfocratio;		
	
	// (SoM) More help from randy. I was totally lost on this... 
	float ixscale = FIXED2FLOAT(finetangent[FINEANGLES/4+FieldOfView/2]) / float(flatwidth);
	float iyscale = FIXED2FLOAT(finetangent[FINEANGLES/4+FieldOfView/2]) / float(flatheight);

	float zat = FIXED2FLOAT(P_PlaneZ(viewx, viewy, &pl->secplane));

	float slopet = (float)tan((90.0f + consoleplayer().fov / 2.0f) * PI / 180.0f);
	float slopevis = 8.0f * slopet * 16.0f * 320.0f / (float)screen->width;
	
	plight = (slopevis * ixscale * iyscale) / (zat - FIXED2FLOAT(viewz));
	shade = 256.0f * 2.0f - (pl->lightlevel + 16.0f) * 256.0f / 128.0f;

	basecolormap = pl->colormap;	// [RH] set basecolormap
   
	pl->top[pl->maxx+1] = 0xffffffffu;
	pl->top[pl->minx-1] = 0xffffffffu;

	// Make spans
	for (int x = pl->minx; x <= pl->maxx + 1; x++)
	{
		unsigned int t1 = pl->top[x-1];
		unsigned int b1 = pl->bottom[x-1];
		unsigned int t2 = pl->top[x];
		unsigned int b2 = pl->bottom[x];
		
		for (; t1 < t2 && t1 <= b1; t1++)
			R_MapSlopedPlane (t1, spanstart[t1], x-1);
		for (; b1 > b2 && b1 >= t1; b1--)
			R_MapSlopedPlane (b1, spanstart[b1] ,x-1);
		while (t2 < t1 && t2 <= b2)
			spanstart[t2++] = x;
		while (b2 > b1 && b2 >= t2)
			spanstart[b2--] = x;
	}
}

void R_DrawLevelPlane(visplane_t *pl)
{
	xscale = pl->xscale;
	yscale = pl->yscale;

	fixed_t cosine = finecosine[pl->angle >> ANGLETOFINESHIFT];
	fixed_t sine = finesine[pl->angle >> ANGLETOFINESHIFT];
	pviewx = FixedMul (viewx, cosine) - FixedMul (viewy, sine) + pl->xoffs;
	pviewy = -(FixedMul (viewx, sine) + FixedMul (viewy, cosine)) + pl->yoffs;

	// left to right mapping
	angle_t angle = (viewangle - ANG90 + pl->angle) >> ANGLETOFINESHIFT;

	// scale will be unit scale at SCREENWIDTH/2 distance
	xstepscale = FixedMul (xscale, FixedDiv (finecosine[angle], FocalLengthX));
	ystepscale = FixedMul (yscale, -FixedDiv (finesine[angle], FocalLengthX));

	baseangle = viewangle + pl->angle;

	basecolormap = pl->colormap;	// [RH] set basecolormap

	// [SL] 2012-02-05 - Plane's height should be constant for all (x,y)
	// so just use (0, 0) when calculating the plane's z height
	planeheight = abs(P_PlaneZ(0, 0, &pl->secplane) - viewz);
	int light = (pl->lightlevel >> LIGHTSEGSHIFT) + (foggy ? 0 : extralight);

	if (light >= LIGHTLEVELS)
		light = LIGHTLEVELS-1;
	else if (light < 0)
		light = 0;

	planezlight = zlight[light];

	pl->top[pl->maxx+1] = 0xffffffffu;
	pl->top[pl->minx-1] = 0xffffffffu;

	// Make Spans
	for (int x = pl->minx; x <= pl->maxx + 1; x++)
	{
		unsigned int t1 = pl->top[x-1];
		unsigned int b1 = pl->bottom[x-1];
		unsigned int t2 = pl->top[x];
		unsigned int b2 = pl->bottom[x];
		
		for (; t1 < t2 && t1 <= b1; t1++)
			R_MapLevelPlane (t1, spanstart[t1], x-1);
		for (; b1 > b2 && b1 >= t1; b1--)
			R_MapLevelPlane (b1, spanstart[b1] ,x-1);
		while (t2 < t1 && t2 <= b2)
			spanstart[t2++] = x;
		while (b2 > b1 && b2 >= t2)
			spanstart[b2--] = x;
	}
}


//
// R_DrawPlanes
//
// At the end of each frame.
//
void R_DrawPlanes (void)
{
	visplane_t *pl;
	int i;

	ds_color = 3;

	for (i = 0; i < MAXVISPLANES; i++)
	{
		for (pl = visplanes[i]; pl; pl = pl->next)
		{
			if (pl->minx > pl->maxx)
				continue;

			// sky flat
			if (pl->picnum == skyflatnum || pl->picnum & PL_SKYFLAT)
			{
				if (pl->picnum == skyflatnum)
				{	// use sky1 [ML] 5/11/06 - Use it always!
					skytex = sky1texture;
					skyflip = 0;
				}
				else if (pl->picnum == int(PL_SKYFLAT))
				{	// use sky2
					skytex = sky2texture;
					skyflip = 0;
				}
				else
				{
					// MBF's linedef-controlled skies
					// Sky Linedef
					short picnum = (pl->picnum & ~PL_SKYFLAT)-1;
					const line_t *l = &lines[picnum < numlines ? picnum : 0];

					// Sky transferred from first sidedef
					const side_t *s = *l->sidenum + sides;

					// Texture comes from upper texture of reference sidedef
					skytex = texturetranslation[s->toptexture];

					// Horizontal offset is turned into an angle offset,
					// to allow sky rotation as well as careful positioning.
					// However, the offset is scaled very small, so that it
					// allows a long-period of sky rotation.
					frontpos = (-s->textureoffset) >> 6;

					// Vertical offset allows careful sky positioning.
					dc_texturemid = s->rowoffset - 28*FRACUNIT;

					// We sometimes flip the picture horizontally.
					//
					// Doom always flipped the picture, so we make it optional,
					// to make it easier to use the new feature, while to still
					// allow old sky textures to be used.
					skyflip = l->args[2] ? 0u : ~0u;
				}

				if (fixedlightlev) {
					dc_colormap = DefaultPalette->maps.colormaps + fixedlightlev;
				} else if (fixedcolormap) {
					if (r_skypalette)
					{
						dc_colormap = fixedcolormap;
					}
					else
					{
						// [SL] 2011-06-28 - Emulate vanilla Doom's handling of skies
						// when the player has the invulnerability powerup
						dc_colormap = DefaultPalette->maps.colormaps;
					}
				} else if (!fixedcolormap) {
					dc_colormap = DefaultPalette->maps.colormaps;
					colfunc = R_StretchColumn;
					hcolfunc_post1 = rt_copy1col;
					hcolfunc_post2 = rt_copy2cols;
					hcolfunc_post4 = rt_copy4cols;
				}

				R_DrawSky (pl);

				colfunc = basecolfunc;
				hcolfunc_post1 = rt_map1col;
				hcolfunc_post2 = rt_map2cols;
				hcolfunc_post4 = rt_map4cols;
			}
			else
			{
				// regular flat
				int useflatnum = flattranslation[pl->picnum < numflats ? pl->picnum : 0];

				ds_color += 4;	// [RH] color if r_drawflat is 1
				ds_source = (byte *)W_CacheLumpNum (firstflat + useflatnum, PU_STATIC);
										   
				// [RH] warp a flat if desired
				if (flatwarp[useflatnum])
				{
					if ((!warpedflats[useflatnum]
						 && Z_Malloc (64*64, PU_STATIC, &warpedflats[useflatnum]))
						|| flatwarpedwhen[useflatnum] != level.time)
					{
						static byte buffer[64];
						int timebase = level.time*23;

						flatwarpedwhen[useflatnum] = level.time;
						byte *warped = warpedflats[useflatnum];

						for (int x = 63; x >= 0; x--)
						{
							int yt, yf = (finesine[(timebase + ((x+17) << 7))&FINEMASK]>>13) & 63;
							byte *source = ds_source + x;
							byte *dest = warped + x;
							for (yt = 64; yt; yt--, yf = (yf+1)&63, dest += 64)
								*dest = *(source + (yf << 6));
						}
						timebase = level.time*32;
						for (int y = 63; y >= 0; y--)
						{
							int xt, xf = (finesine[(timebase + (y << 7))&FINEMASK]>>13) & 63;
							byte *source = warped + (y << 6);
							byte *dest = buffer;
							for (xt = 64; xt; xt--, xf = (xf+1) & 63)
								*dest++ = *(source+xf);
							memcpy (warped + (y << 6), buffer, 64);
						}
						Z_ChangeTag (ds_source, PU_CACHE);
						ds_source = warped;
					}
					else
					{
						Z_ChangeTag (ds_source, PU_CACHE);
						ds_source = warpedflats[useflatnum];
						Z_ChangeTag (ds_source, PU_STATIC);
					}
				}
				
#ifdef USEASM
				if (ds_source != ds_cursource)
					R_SetSpanSource_ASM (ds_source);
#endif

				if (P_IsPlaneLevel(&pl->secplane))
					R_DrawLevelPlane(pl);
				else
					R_DrawSlopedPlane(pl);
					
				Z_ChangeTag (ds_source, PU_CACHE);
			}
		}
	}
}

//
// R_PlaneInitData
//
BOOL R_PlaneInitData (void)
{
	int i;
	visplane_t *pl;

	delete[] floorclip;
	delete[] ceilingclip;
	delete[] spanstart;
	delete[] yslope;
	delete[] distscale;

	floorclip = new int[screen->width];
	ceilingclip = new int[screen->width];

	spanstart = new int[screen->height];

	yslope = new fixed_t[screen->height];
	distscale = new fixed_t[screen->width];

	// Free all visplanes and let them be re-allocated as needed.
	pl = freetail;

	while (pl)
	{
		visplane_t *next = pl->next;
		M_Free(pl);
		pl = next;
	}
	freetail = NULL;
	freehead = &freetail;

	for (i = 0; i < MAXVISPLANES; i++)
	{
		pl = visplanes[i];
		visplanes[i] = NULL;
		while (pl)
		{
			visplane_t *next = pl->next;
			M_Free(pl);
			pl = next;
		}
	}

	return true;
}

//
// R_AlignFlat
//
BOOL R_AlignFlat (int linenum, int side, int fc)
{
	line_t *line = lines + linenum;
	sector_t *sec = side ? line->backsector : line->frontsector;

	if (!sec)
		return false;

	fixed_t x = line->v1->x;
	fixed_t y = line->v1->y;

	angle_t angle = R_PointToAngle2 (x, y, line->v2->x, line->v2->y);
	angle_t norm = (angle-ANG90) >> ANGLETOFINESHIFT;

	fixed_t dist = -FixedMul (finecosine[norm], x) - FixedMul (finesine[norm], y);

	if (side)
	{
		angle = angle + ANG180;
		dist = -dist;
	}

	if (fc)
	{
		sec->base_ceiling_angle = 0-angle;
		sec->base_ceiling_yoffs = dist & ((1<<(FRACBITS+8))-1);
	}
	else
	{
		sec->base_floor_angle = 0-angle;
		sec->base_floor_yoffs = dist & ((1<<(FRACBITS+8))-1);
	}

	return true;
}

VERSION_CONTROL (r_plane_cpp, "$Id: r_plane.cpp 3174 2012-05-11 01:03:43Z mike $")
