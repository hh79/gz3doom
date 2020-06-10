/*
** palette.cpp
** Palette and color utility functions
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <algorithm>
#include "palutil.h"
#include "palentry.h"
#include "sc_man.h"
#include "files.h"

/****************************/
/* Palette management stuff */
/****************************/

int BestColor (const uint32_t *pal_in, int r, int g, int b, int first, int num)
{
	const PalEntry *pal = (const PalEntry *)pal_in;
	int bestcolor = first;
	int bestdist = 257 * 257 + 257 * 257 + 257 * 257;

	for (int color = first; color < num; color++)
	{
		int x = r - pal[color].r;
		int y = g - pal[color].g;
		int z = b - pal[color].b;
		int dist = x*x + y*y + z*z;
		if (dist < bestdist)
		{
			if (dist == 0)
				return color;

			bestdist = dist;
			bestcolor = color;
		}
	}
	return bestcolor;
}


// [SP] Re-implemented BestColor for more precision rather than speed. This function is only ever called once until the game palette is changed.

int PTM_BestColor (const uint32_t *pal_in, int r, int g, int b, bool reverselookup, float powtable_val, int first, int num)
{
	const PalEntry *pal = (const PalEntry *)pal_in;
	static double powtable[256];
	static bool firstTime = true;
	static float trackpowtable = 0.;

	double fbestdist = DBL_MAX, fdist;
	int bestcolor = 0;

	if (firstTime || trackpowtable != powtable_val)
	{
		auto pt = powtable_val;
		trackpowtable = pt;
		firstTime = false;
		for (int x = 0; x < 256; x++) powtable[x] = pow((double)x/255, (double)pt);
	}

	for (int color = first; color < num; color++)
	{
		double x = powtable[abs(r-pal[color].r)];
		double y = powtable[abs(g-pal[color].g)];
		double z = powtable[abs(b-pal[color].b)];
		fdist = x + y + z;
		if (color == first || (reverselookup?(fdist <= fbestdist):(fdist < fbestdist)))
		{
			if (fdist == 0 && !reverselookup)
				return color;

			fbestdist = fdist;
			bestcolor = color;
		}
	}
	return bestcolor;
}

#if defined(_M_X64) || defined(_M_IX86) || defined(__i386__) || defined(__amd64__)

#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <emmintrin.h>

static void DoBlending_SSE2(const PalEntry *from, PalEntry *to, int count, int r, int g, int b, int a)
{
	__m128i blendcolor;
	__m128i blendalpha;
	__m128i zero;
	__m128i blending256;
	__m128i color1;
	__m128i color2;
	size_t unaligned;

	unaligned = ((size_t)from | (size_t)to) & 0xF;

#if defined(__amd64__) || defined(_M_X64)
	int64_t color;

	blending256 = _mm_set_epi64x(0x10001000100ll, 0x10001000100ll);

	color = ((int64_t)r << 32) | (g << 16) | b;
	blendcolor = _mm_set_epi64x(color, color);

	color = ((int64_t)a << 32) | (a << 16) | a;
	blendalpha = _mm_set_epi64x(color, color);
#else
	int color;

	blending256 = _mm_set_epi32(0x100, 0x1000100, 0x100, 0x1000100);

	color = (g << 16) | b;
	blendcolor = _mm_set_epi32(r, color, r, color);

	color = (a << 16) | a;
	blendalpha = _mm_set_epi32(a, color, a, color);
#endif

	blendcolor = _mm_mullo_epi16(blendcolor, blendalpha);	// premultiply blend by alpha
	blendalpha = _mm_subs_epu16(blending256, blendalpha);	// one minus alpha

	zero = _mm_setzero_si128();

	if (unaligned)
	{
		for (count >>= 2; count > 0; --count)
		{
			color1 = _mm_loadu_si128((__m128i *)from);
			from += 4;
			color2 = _mm_unpackhi_epi8(color1, zero);
			color1 = _mm_unpacklo_epi8(color1, zero);
			color1 = _mm_mullo_epi16(blendalpha, color1);
			color2 = _mm_mullo_epi16(blendalpha, color2);
			color1 = _mm_adds_epu16(blendcolor, color1);
			color2 = _mm_adds_epu16(blendcolor, color2);
			color1 = _mm_srli_epi16(color1, 8);
			color2 = _mm_srli_epi16(color2, 8);
			_mm_storeu_si128((__m128i *)to, _mm_packus_epi16(color1, color2));
			to += 4;
		}
	}
	else
	{
		for (count >>= 2; count > 0; --count)
		{
			color1 = _mm_load_si128((__m128i *)from);
			from += 4;
			color2 = _mm_unpackhi_epi8(color1, zero);
			color1 = _mm_unpacklo_epi8(color1, zero);
			color1 = _mm_mullo_epi16(blendalpha, color1);
			color2 = _mm_mullo_epi16(blendalpha, color2);
			color1 = _mm_adds_epu16(blendcolor, color1);
			color2 = _mm_adds_epu16(blendcolor, color2);
			color1 = _mm_srli_epi16(color1, 8);
			color2 = _mm_srli_epi16(color2, 8);
			_mm_store_si128((__m128i *)to, _mm_packus_epi16(color1, color2));
			to += 4;
		}
	}
}
#endif

void DoBlending (const PalEntry *from, PalEntry *to, int count, int r, int g, int b, int a)
{
	if (a == 0)
	{
		if (from != to)
		{
			memcpy (to, from, count * sizeof(uint32_t));
		}
		return;
	}
	else if (a == 256)
	{
		uint32_t t = MAKERGB(r,g,b);
		int i;

		for (i = 0; i < count; i++)
		{
			to[i] = t;
		}
		return;
	}
#if defined(_M_X64) || defined(_M_IX86) || defined(__i386__) || defined(__amd64__)
	else if (count >= 4)
	{
		int not3count = count & ~3;
		DoBlending_SSE2 (from, to, not3count, r, g, b, a);
		count &= 3;
		if (count <= 0)
		{
			return;
		}
		from += not3count;
		to += not3count;
	}
#endif
	int i, ia;

	ia = 256 - a;
	r *= a;
	g *= a;
	b *= a;

	for (i = count; i > 0; i--, to++, from++)
	{
		to->r = (r + from->r * ia) >> 8;
		to->g = (g + from->g * ia) >> 8;
		to->b = (b + from->b * ia) >> 8;
	}
}

/****** Colorspace Conversion Functions ******/

// Code from http://www.cs.rit.edu/~yxv4997/t_convert.html

// r,g,b values are from 0 to 1
// h = [0,360], s = [0,1], v = [0,1]
//				if s == 0, then h = -1 (undefined)

// Green Doom guy colors:
// RGB - 0: {    .46  1 .429 } 7: {    .254 .571 .206 } 15: {    .0317 .0794 .0159 }
// HSV - 0: { 116.743 .571 1 } 7: { 112.110 .639 .571 } 15: { 105.071  .800 .0794 }
void RGBtoHSV (float r, float g, float b, float *h, float *s, float *v)
{
	float min, max, delta, foo;

	if (r == g && g == b)
	{
		*h = 0;
		*s = 0;
		*v = r;
		return;
	}

	foo = r < g ? r : g;
	min = (foo < b) ? foo : b;
	foo = r > g ? r : g;
	max = (foo > b) ? foo : b;

	*v = max;									// v

	delta = max - min;

	*s = delta / max;							// s

	if (r == max)
		*h = (g - b) / delta;					// between yellow & magenta
	else if (g == max)
		*h = 2 + (b - r) / delta;				// between cyan & yellow
	else
		*h = 4 + (r - g) / delta;				// between magenta & cyan

	*h *= 60;									// degrees
	if (*h < 0)
		*h += 360;
}

void HSVtoRGB (float *r, float *g, float *b, float h, float s, float v)
{
	int i;
	float f, p, q, t;

	if (s == 0)
	{ // achromatic (grey)
		*r = *g = *b = v;
		return;
	}

	h /= 60;									// sector 0 to 5
	i = (int)floor (h);
	f = h - i;									// factorial part of h
	p = v * (1 - s);
	q = v * (1 - s * f);
	t = v * (1 - s * (1 - f));

	switch (i)
	{
	case 0:		*r = v; *g = t; *b = p; break;
	case 1:		*r = q; *g = v; *b = p; break;
	case 2:		*r = p; *g = v; *b = t; break;
	case 3:		*r = p; *g = q; *b = v; break;
	case 4:		*r = t; *g = p; *b = v; break;
	default:	*r = v; *g = p; *b = q; break;
	}
}

static int sortforremap(const void* a, const void* b)
{
	return (*(const uint32_t*)a & 0xFFFFFF) - (*(const uint32_t*)b & 0xFFFFFF);
}

static int sortforremap2(const void* a, const void* b)
{
	const RemappingWork* ap = (const RemappingWork*)a;
	const RemappingWork* bp = (const RemappingWork*)b;

	if (ap->Color == bp->Color)
	{
		return bp->Foreign - ap->Foreign;
	}
	else
	{
		return ap->Color - bp->Color;
	}
}


struct RemappingWork
{
	uint32_t Color;
	uint8_t Foreign;	// 0 = local palette, 1 = foreign palette
	uint8_t PalEntry;	// Entry # in the palette
	uint8_t Pad[2];
};

void MakeRemap(uint32_t* BaseColors, const uint32_t* colors, uint8_t* remap, const uint8_t* useful, int numcolors) 
{
	RemappingWork workspace[255 + 256];
	int i, j, k;

	// Fill in workspace with the colors from the passed palette and this palette.
	// By sorting this array, we can quickly find exact matches so that we can
	// minimize the time spent calling BestColor for near matches.

	for (i = 1; i < 256; ++i)
	{
		workspace[i - 1].Color = uint32_t(BaseColors[i]) & 0xFFFFFF;
		workspace[i - 1].Foreign = 0;
		workspace[i - 1].PalEntry = i;
	}
	for (i = k = 0, j = 255; i < numcolors; ++i)
	{
		if (useful == NULL || useful[i] != 0)
		{
			workspace[j].Color = colors[i] & 0xFFFFFF;
			workspace[j].Foreign = 1;
			workspace[j].PalEntry = i;
			++j;
			++k;
		}
		else
		{
			remap[i] = 0;
		}
	}
	qsort(workspace, j, sizeof(RemappingWork), sortforremap2);

	// Find exact matches
	--j;
	for (i = 0; i < j; ++i)
	{
		if (workspace[i].Foreign)
		{
			if (!workspace[i + 1].Foreign && workspace[i].Color == workspace[i + 1].Color)
			{
				remap[workspace[i].PalEntry] = workspace[i + 1].PalEntry;
				workspace[i].Foreign = 2;
				++i;
				--k;
			}
		}
	}

	// Find near matches
	if (k > 0)
	{
		for (i = 0; i <= j; ++i)
		{
			if (workspace[i].Foreign == 1)
			{
				remap[workspace[i].PalEntry] = BestColor((uint32_t*)BaseColors,
					RPART(workspace[i].Color), GPART(workspace[i].Color), BPART(workspace[i].Color),
					1, 255);
			}
		}
	}
}

// In ZDoom's new texture system, color 0 is used as the transparent color.
// But color 0 is also a valid color for Doom engine graphics. What to do?
// Simple. The default palette for every game has at least one duplicate
// color, so find a duplicate pair of palette entries, make one of them a
// duplicate of color 0, and remap every graphic so that it uses that entry
// instead of entry 0.
void MakeGoodRemap(uint32_t* BaseColors, uint8_t* Remap)
{
	for (int i = 0; i < 256; i++) Remap[i] = i;
	PalEntry color0 = BaseColors[0];
	int i;

	// First try for an exact match of color 0. Only Hexen does not have one.
	for (i = 1; i < 256; ++i)
	{
		if (BaseColors[i] == color0)
		{
			Remap[0] = i;
			break;
		}
	}

	// If there is no duplicate of color 0, find the first set of duplicate
	// colors and make one of them a duplicate of color 0. In Hexen's PLAYPAL
	// colors 209 and 229 are the only duplicates, but we cannot assume
	// anything because the player might be using a custom PLAYPAL where those
	// entries are not duplicates.
	if (Remap[0] == 0)
	{
		PalEntry sortcopy[256];

		for (i = 0; i < 256; ++i)
		{
			sortcopy[i] = BaseColors[i] | (i << 24);
		}
		qsort(sortcopy, 256, 4, sortforremap);
		for (i = 255; i > 0; --i)
		{
			if ((sortcopy[i] & 0xFFFFFF) == (sortcopy[i - 1] & 0xFFFFFF))
			{
				int new0 = sortcopy[i].a;
				int dup = sortcopy[i - 1].a;
				if (new0 > dup)
				{
					// Make the lower-numbered entry a copy of color 0. (Just because.)
					std::swap(new0, dup);
				}
				Remap[0] = new0;
				Remap[new0] = dup;
				BaseColors[new0] = color0;
				break;
			}
		}
	}

	// If there were no duplicates, InitPalette() will remap color 0 to the
	// closest matching color. Hopefully nobody will use a palette where all
	// 256 entries are different. :-)
}

