#pragma once

#include "stdio.h"
#include <vector>
#include <string>
#include "InGameFunctions.h"
#include "GlobalVariables.h"
#include "CarSlotID.h"
#include "includes\injector\injector.hpp"

// ---------------------------------------------------------------------------------------------
// Tyre texture, chosen by a part, applied per material
//
// A tyre texture of its own (Toyo lettering, a cut Bridgestone) without repainting the rim, on
// unmodified wheel geometry.
//
// WHAT THE GEOMETRY SAYS. 5ZIGEN_STYLE01_15_23_A out of CARS\WHEELS\GEOMETRY_5ZIGEN.BIN:
//
//     textures (1):          [0] 0x733AE956
//     light materials (3):   [0] RUBBER  [1] 0x010CB64A  [2] MAGCHROME
//     materials (3):         tex 0 + RUBBER      120 tris   the tyre
//                            tex 0 + MAGCHROME   355 tris   the rim
//                            tex 0 + 0x010CB64A   22 tris
//
// One texture slot, three materials. Rim and tyre share the texture, so the light material name
// is the only thing separating them and the override has to key on it.
//
// WHY THE VANILLA REPLACEMENT TABLE CANNOT DO THIS. eModel::AttachReplacementTextureTable
// (0x488E20) stores the table at eModel+0x10 with its count at +0x14, and sub_4905A0 applies it by
// walking the SOLID's texture slots and swapping a slot whose hash matches an entry's old hash.
// Keyed by texture hash, applied per slot. With one shared slot it would repaint the rim as well.
//
// WHERE THE SEPARATION IS. eViewPlatInterface::Render (0x5C5930) draws one mesh at a time and has
// both halves in hand at 0x5C5A8C:
//
//     movzx edx, byte ptr [esi+1Ch]      ; mesh entry +0x1C = texture index
//     mov   eax, [esp+10h]               ; eSolid
//     mov   ecx, [eax+2Ch]               ; texture table, entries { hash, TextureInfo* }
//     mov   ebp, [ecx+edx*8+4]           ; the TextureInfo this mesh will draw with
//     ...
//     mov   al, [esi+20h]                ; mesh entry +0x20 = light material index, 0xFF = none
//     mov   edx, [eax+3Ch]               ; light materials, entries { nameHash, eLightMaterial* }
//
// Swapping ebp there is a per material texture override, which is the thing the replacement table
// cannot express. Mesh entries are the file's 60 byte shading groups loaded in place: the loop
// advances by 0x3C, count at [eSolid+0][0x10], array at [eSolid+0][0x14].
//
// WHICH TEXTURE. The part sitting in the tyre slot decides, through its TEXTURE_NAME attribute
// (0x10C98090), which is the same attribute a rim uses to name its own texture. So a tyre is
// authored like any other part: a car part with TEXTURE_NAME pointing at a texture.
//
// WHICH SLOT. The parts live in LEFT_SIDE_MIRROR, and that costs nothing: UG2 hangs both mirrors
// off WING_MIRROR and never uses LEFT_SIDE_MIRROR or RIGHT_SIDE_MIRROR for a model, so a car
// wearing a tyre part loses nothing. Changing TIRE_CAR_SLOT below and re-authoring the parts with
// the matching CarPartGroupID is the whole move if it ever has to go elsewhere.
//
// HOW THE PARTS REACH EVERY CAR. GetTypesFromSlot (0x6101E0) hands NewGetCarPart two car type
// names to search per slot, out of DefaultSlotTypeNameTable, where 0xFFFFFFFF means "the car's own
// type". Writing the tyre collection's name hash into the second entry of the tyre slot is what
// makes one shared set of tyre parts visible on every car, the same way the game already shares
// other parts. The car's own type stays first, so a car's own part in that slot still wins.
//
// SETTINGS
//
//   [Misc] TirePartsCollection = TIRES
//     the car type name that holds the tyre parts. Off when empty: nothing is offered and nothing
//     is filtered.
//
//   [Debug] TireTexture = CARBONFIBRE
//     forces one texture on every tyre in the game, ignoring parts. A test aid, not the feature.
//
//   [Debug] TireMaterialProbe = 1
//     writes UnlimiterData\_TireProbe.txt: for each solid carrying a RUBBER material, its texture
//     slots and light material names. Costs a write the first time each solid is drawn, so every
//     LOD change stutters while it is on. Leave it off unless something is wrong.
// ---------------------------------------------------------------------------------------------

// The slot the tyre parts occupy. One line, because which slot to spend is a modding decision and
// not an engine fact: the parts' CarPartGroupID has to match whatever this says.
#define TIRE_CAR_SLOT CARSLOTID_LEFT_SIDE_MIRROR

#define TIRE_MATERIAL_RUBBER    CT_bStringHash("RUBBER")        // 0x78743CA1
#define TIRE_ATTR_TEXTURE_NAME  CT_bStringHash("TEXTURE_NAME")  // 0x10C98090
#define TIRE_ATTR_PAINTABLE     CT_bStringHash("PAINTABLE")     // 0x0EF2522F

#define _DefaultSlotTypeNameTable 0x8A1CE8

// eSolid, as the renderer reads it
#define eSolid_NumTextures(s)        (*(BYTE*)((BYTE*)(s) + 0x19))
#define eSolid_NumLightMaterials(s)  (*(char*)((BYTE*)(s) + 0x1A))
#define eSolid_TextureTable(s)       (*(DWORD**)((BYTE*)(s) + 0x2C))
#define eSolid_LightMaterials(s)     (*(DWORD**)((BYTE*)(s) + 0x3C))

// one 60 byte mesh entry, the runtime face of a shading group
#define eMesh_TextureIndex(m)        (*(BYTE*)((BYTE*)(m) + 0x1C))
#define eMesh_LightMaterialIndex(m)  (*(BYTE*)((BYTE*)(m) + 0x20))

bool TireMaterialProbe = false;
DWORD TireTextureOverride = 0;
DWORD TirePartsCollection = 0;

// What the mesh hook reads. TireHookLive is the one word the shim tests, so a car with no tyre
// part costs nothing past a compare.
void* TireCurrentTexture = nullptr;
DWORD TireHookLive = 0;

// Held open. Opening and closing per line is what made every LOD change stutter with the probe on,
// because a new LOD is a new solid and a new solid is a dozen lines.
FILE* TireProbeFile = nullptr;

void TireProbeLine(const char* fmt, ...)
{
	if (!TireMaterialProbe) return;

	if (!TireProbeFile)
	{
		auto Path = CurrentWorkingDirectory / "UnlimiterData" / "_TireProbe.txt";
		TireProbeFile = fopen(Path.string().c_str(), "a");
		if (!TireProbeFile) return;
	}

	va_list args;
	va_start(args, fmt);
	vfprintf(TireProbeFile, fmt, args);
	va_end(args);

	fflush(TireProbeFile);
}

bool Tire_ValidPtr(void* p)
{
	uintptr_t v = (uintptr_t)p;
	return v >= 0x00010000 && v <= 0xC0000000 && !(v & 3);
}

// Car parts are 14 byte records packed back to back, so every other one sits on an address that
// is only even. Tire_ValidPtr's alignment test would turn those away, and did: a PAINTABLE tyre
// that landed on one was never painted and said nothing about it. Range only for parts.
bool Tire_ValidPartPtr(void* p)
{
	uintptr_t v = (uintptr_t)p;
	return v >= 0x00010000 && v <= 0xC0000000;
}

// One entry per solid, not one per mesh per frame. This sits in the renderer's inner loop.
std::vector<DWORD*> TireProbeSeen;

bool Tire_AlreadyProbed(DWORD* Solid)
{
	for (size_t i = 0; i < TireProbeSeen.size(); i++)
		if (TireProbeSeen[i] == Solid) return true;

	if (TireProbeSeen.size() < 128) TireProbeSeen.push_back(Solid);

	return false;
}

void Tire_ProbeSolid(DWORD* Solid)
{
	if (!TireMaterialProbe) return;
	if (Tire_AlreadyProbed(Solid)) return;

	int Textures = eSolid_NumTextures(Solid);
	int Materials = eSolid_NumLightMaterials(Solid);
	DWORD* TextureTable = eSolid_TextureTable(Solid);
	DWORD* MaterialTable = eSolid_LightMaterials(Solid);

	TireProbeLine("solid %p: %d texture slot(s), %d light material(s)\n", Solid, Textures, Materials);

	if (Tire_ValidPtr(TextureTable))
		for (int i = 0; i < Textures && i < 64; i++)
			TireProbeLine("    texture [%d] hash 0x%08X -> TextureInfo %p\n",
				i, (unsigned int)TextureTable[i * 2], (void*)TextureTable[i * 2 + 1]);

	if (Tire_ValidPtr(MaterialTable))
		for (int i = 0; i < Materials && i < 64; i++)
			TireProbeLine("    light material [%d] 0x%08X%s\n",
				i, (unsigned int)MaterialTable[i * 2],
				MaterialTable[i * 2] == TIRE_MATERIAL_RUBBER ? " RUBBER" : "");
}

// The texture a ride's tyre part names, or 0. The debug override wins so one ini line can prove
// the render path with no part involved at all.
DWORD Tire_TextureHashForRide(DWORD* RideInfo)
{
	if (TireTextureOverride) return TireTextureOverride;
	if (!Tire_ValidPtr(RideInfo)) return 0;

	DWORD* Part = (DWORD*)RideInfo[356 + TIRE_CAR_SLOT];
	if (!Part) return 0;

	return CarPart_GetTextureName(Part);
}

bool Tire_IsPartListable(DWORD* Part, int Slot)
{
	if (Slot != TIRE_CAR_SLOT) return true;
	if (!TirePartsCollection) return true;

	// The browser walks the car's own type before the tyre collection, so without this the tyre
	// category would also list whatever parts the car itself owns in this slot.
	return (DWORD)CarPart_GetCarTypeNameHash(Part) == TirePartsCollection;
}


// Each distinct probe line is written once. A one shot flag per message was not enough: the first
// car through, often a preview with no tyre part at all, used it up and every later car, the one
// actually being looked at included, went unreported. Keeping the lines themselves means each car
// and each outcome gets said once however many cars alternate every frame.
std::vector<std::string> TireNotesWritten;

void TireNote(const char* fmt, ...)
{
	if (!TireMaterialProbe) return;
	if (TireNotesWritten.size() >= 400) return;

	char Line[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(Line, sizeof(Line), fmt, args);
	va_end(args);

	for (auto& L : TireNotesWritten)
		if (L == Line) return;

	TireNotesWritten.push_back(Line);
	TireProbeLine("%s", Line);
}

// PAINTABLE TYRES, coloured the way vinyls are.
//
// A vinyl is a channel mix. CompositeSkin builds four colours per vinyl layer, one per texture
// channel, and when no colour has been chosen for a channel it fills in that channel's own pure
// colour (0x61DC8F, 0xFF shifted by 0, 8, 16 and 24), so an unpainted vinyl mixes back into exactly
// what was authored. Red in the texture becomes colour one, green colour two, and so on, with the
// strength of the channel as the shade. A tyre has one colour to offer, so only red is remapped and
// green and blue stay themselves: author the stripe in red, and shades of red come out as shades of
// the colour.
//
// The colour is the part in WHEEL_MANUFACTURER, the one the tyre smoke reads RED, GREEN and BLUE
// from, so the two are in step by construction. VINYL_L1_COLOR01 counts as none, the same as in
// ApplyTireSmokeColor, and none leaves the tyre as authored.
//
// Where the mix applies is <TEXTURE>_MASK, a greyscale coverage map like a vinyl's own mask: black
// keeps the texture, white takes the mix, grey blends. Without the mask the rubber would be mixed
// as well, and grey rubber has a red component. bStringHash2("_MASK", h) builds the mask's hash from
// the texture's, since bStringHash is a fold: TIRE_PAINTABLE is A2BEB302, TIRE_PAINTABLE_MASK
// A07354CD.
//
// FORMATS. What the texture is, and how big, is read off the D3D texture itself with
// IDirect3DTexture9::GetLevelCount (+34h) and GetLevelDesc (+44h), not out of TextureInfo. The
// game can hand D3D a smaller texture than TextureInfo describes: under a lower texture detail
// setting sub_5CE170 divides both sides and leaves out that many top levels (0x5CE297,
// dword_870794), so TextureInfo's sizes are not what LockRect hands back. Paletted textures never
// reach D3D as such, the same function expands them to A8R8G8B8 at creation (0x5CE1BC to
// 0x5CE271), so a P8 tyre arrives here as 32 bit.
//
// A8R8G8B8 and X8R8G8B8 are a plain array of pixels, blue, green, red, alpha in memory, painted in
// place.
//
// DXT1, DXT3 and DXT5 store every 4x4 block as two 5:6:5 colours and a 2 bit index per texel
// choosing one of four points between them. DXT3 and DXT5 keep their alpha in the first half of a
// 16 byte block and DXT1 has only the 8 byte colour half. Alpha is never touched. The colour half
// is rewritten only in blocks the mask reaches, every other block goes back bit for bit, and a
// painted block is encoded two ways with the closer one kept:
//   - the block's own two colours put through the mix. The mix is linear in the colour, so where
//     the mask is even across the block this is the authored block recoloured, with nothing lost
//     to a second compression;
//   - a fresh fit along the painted texels' principal axis, refined by least squares, for blocks a
//     mask edge runs through, whose texels no longer share one mix.
// A DXT1 block using its transparent texel (first colour not the larger, index 3) is left as it
// is, since a four colour block cannot hold one. DXT3 and DXT5 colour halves are always read as
// four colour, the way D3D reads them.
//
// The mask can be any of those formats and any size. Coverage is its brightest channel, and each
// texel of the tyre takes the mask's average over the area it covers.
//
// Every mip level is painted, each against the mask scaled to its own size, so the stripe keeps
// its colour at a distance too.
//
// CompositeWheel is not used for this, for three reasons. Its 32 bit path pairs RED with the
// pixel's first byte, which is blue, so it would recolour the wrong channel. It multiplies rather
// than mixing channels, so a red stripe times a blue colour goes black instead of blue. And it locks
// its source and destination separately, and a D3D surface that is already locked cannot be locked
// again, so one texture cannot be both.
//
// The pass writes over the texture it reads, so the untouched pixels are kept the first time a
// texture is seen and every pass starts from them. Otherwise each colour change would mix the
// previous result again.
//
// Locking goes straight to the texture's own vtable, LockRect at +4Ch and UnlockRect at +50h, the
// same calls TextureInfoPlatInterface::LockImage (0x5B96A0) and sub_5B96D0 make. The game's wrapper
// throws away both the HRESULT and the row pitch and only ever locks level 0; this needs the first
// to know the pointer is real, the second to walk rows correctly, and every level. Reads lock with
// D3DLOCK_READONLY, so D3D does not count them as changes and upload the texture again.
//
// Textures are looked up with GetTextureInfo(hash, 0, 0), which returns null for a texture no loaded
// pack holds. With 1 as the second argument it hands back DefaultTextureInfo instead (0x4902AF),
// which would be painted in place of the real thing.

#define TIRE_D3DFMT_A8R8G8B8 21
#define TIRE_D3DFMT_X8R8G8B8 22
#define TIRE_D3DFMT_DXT1 0x31545844 // 'DXT1'
#define TIRE_D3DFMT_DXT2 0x32545844
#define TIRE_D3DFMT_DXT3 0x33545844
#define TIRE_D3DFMT_DXT4 0x34545844
#define TIRE_D3DFMT_DXT5 0x35545844
#define TIRE_D3DLOCK_READONLY 0x10

enum { TIRE_KIND_NONE, TIRE_KIND_32BIT, TIRE_KIND_DXT1, TIRE_KIND_DXT35 };

struct TireSurfaceDesc { DWORD Format, Type, Usage, Pool, MultiSampleType, MultiSampleQuality; UINT Width, Height; };

struct TireLockedRect { int Pitch; BYTE* Bits; };

// One mip level as this code walks it: pixel rows for 32 bit, rows of 4x4 blocks for DXT.
struct TireLevel
{
	int Kind;
	DWORD Format;
	int Width, Height;
	int Rows, RowBytes, BlockBytes;
};

void* Tire_D3DTexture(void* TextureInfo)
{
	if (!Tire_ValidPtr(TextureInfo)) return nullptr;

	BYTE* Plat = *(BYTE**)TextureInfo;

	if (!Tire_ValidPtr(Plat)) return nullptr;

	void* Texture = *(void**)(Plat + 0x18);

	return Tire_ValidPtr(Texture) ? Texture : nullptr;
}

int Tire_LevelCount(void* Texture)
{
	typedef DWORD(__stdcall* GetLevelCountFn)(void*);

	return (int)((GetLevelCountFn)(*(void***)Texture)[0x34 / 4])(Texture);
}

// False only when D3D will not describe the level. An unsupported format comes back as true with
// Kind left at TIRE_KIND_NONE, so the caller can name the format it turned down.
bool Tire_LevelLayout(void* Texture, int Level, TireLevel& L)
{
	typedef long(__stdcall* GetLevelDescFn)(void*, UINT, TireSurfaceDesc*);

	TireSurfaceDesc Desc = {};
	L = {};

	if (((GetLevelDescFn)(*(void***)Texture)[0x44 / 4])(Texture, (UINT)Level, &Desc) < 0) return false;

	L.Format = Desc.Format;
	L.Width = (int)Desc.Width;
	L.Height = (int)Desc.Height;

	if (L.Width <= 0 || L.Height <= 0) return false;

	switch (Desc.Format)
	{
	case TIRE_D3DFMT_A8R8G8B8:
	case TIRE_D3DFMT_X8R8G8B8:
		L.Kind = TIRE_KIND_32BIT;
		L.BlockBytes = 4;
		L.Rows = L.Height;
		L.RowBytes = L.Width * 4;
		return true;

	case TIRE_D3DFMT_DXT1:
		L.Kind = TIRE_KIND_DXT1;
		L.BlockBytes = 8;
		break;

	case TIRE_D3DFMT_DXT2:
	case TIRE_D3DFMT_DXT3:
	case TIRE_D3DFMT_DXT4:
	case TIRE_D3DFMT_DXT5:
		L.Kind = TIRE_KIND_DXT35;
		L.BlockBytes = 16;
		break;

	default:
		return true;
	}

	L.Rows = (L.Height + 3) / 4;
	L.RowBytes = ((L.Width + 3) / 4) * L.BlockBytes;

	return true;
}

const char* Tire_FormatName(DWORD Format, char* Buf)
{
	if (Format == TIRE_D3DFMT_A8R8G8B8) return "A8R8G8B8";
	if (Format == TIRE_D3DFMT_X8R8G8B8) return "X8R8G8B8";

	if (Format > 0xFFFF)
	{
		for (int i = 0; i < 4; i++) Buf[i] = (char)((Format >> (8 * i)) & 0xFF);
		Buf[4] = 0;
	}
	else snprintf(Buf, 16, "D3DFMT %u", (unsigned int)Format);

	return Buf;
}

bool Tire_Lock(void* Texture, int Level, int RowBytes, TireLockedRect& Rect, long& Result, DWORD Flags)
{
	typedef long(__stdcall* LockRectFn)(void*, unsigned int, TireLockedRect*, const void*, unsigned long);
	typedef long(__stdcall* UnlockRectFn)(void*, unsigned int);

	Rect.Pitch = 0;
	Rect.Bits = nullptr;

	Result = ((LockRectFn)(*(void***)Texture)[0x4C / 4])(Texture, (unsigned int)Level, &Rect, nullptr, Flags);

	if (Result < 0) return false;

	if (!Rect.Bits || Rect.Pitch < RowBytes)
	{
		((UnlockRectFn)(*(void***)Texture)[0x50 / 4])(Texture, (unsigned int)Level);
		return false;
	}

	return true;
}

void Tire_Unlock(void* Texture, int Level)
{
	typedef long(__stdcall* UnlockRectFn)(void*, unsigned int);

	((UnlockRectFn)(*(void***)Texture)[0x50 / 4])(Texture, (unsigned int)Level);
}

static inline int TireClamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

struct TireRGB { int r, g, b; };

static inline bool operator==(const TireRGB& a, const TireRGB& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static inline int Tire_Distance(const TireRGB& a, const TireRGB& b)
{
	int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
	return dr * dr + dg * dg + db * db;
}

// The vinyl mix with only the first colour set: red becomes the colour, scaled by how red the
// texel was, green and blue carry on as themselves, and the mask's coverage blends it in.
static inline TireRGB Tire_Mix(const TireRGB& p, int Cover, int CR, int CG, int CB)
{
	if (!Cover) return p;

	int nr = TireClamp(p.r * CR / 255);
	int ng = TireClamp(p.r * CG / 255 + p.g);
	int nb = TireClamp(p.r * CB / 255 + p.b);

	return { p.r + (nr - p.r) * Cover / 255, p.g + (ng - p.g) * Cover / 255, p.b + (nb - p.b) * Cover / 255 };
}

static inline TireRGB Tire_Expand565(WORD c)
{
	int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;

	return { (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2) };
}

static inline WORD Tire_Pack565(int r, int g, int b)
{
	r = (TireClamp(r) * 31 + 127) / 255;
	g = (TireClamp(g) * 63 + 127) / 255;
	b = (TireClamp(b) * 31 + 127) / 255;

	return (WORD)((r << 11) | (g << 5) | b);
}

static inline WORD Tire_Pack565(const double* c)
{
	return Tire_Pack565((int)(c[0] + 0.5), (int)(c[1] + 0.5), (int)(c[2] + 0.5));
}

static void Tire_Palette4(WORD c0, WORD c1, TireRGB Pal[4])
{
	Pal[0] = Tire_Expand565(c0);
	Pal[1] = Tire_Expand565(c1);
	Pal[2] = { (2 * Pal[0].r + Pal[1].r) / 3, (2 * Pal[0].g + Pal[1].g) / 3, (2 * Pal[0].b + Pal[1].b) / 3 };
	Pal[3] = { (Pal[0].r + 2 * Pal[1].r) / 3, (Pal[0].g + 2 * Pal[1].g) / 3, (Pal[0].b + 2 * Pal[1].b) / 3 };
}

// Decodes a colour half into Px. FourColour says the block is read as four colours, which is what
// makes its own endpoints worth reusing; Opaque is false for a DXT1 block using its transparent
// texel.
static void Tire_DecodeColour(const BYTE* Block, bool Dxt1, TireRGB Px[16], int Idx[16], bool& FourColour, bool& Opaque)
{
	WORD c0 = *(const WORD*)Block, c1 = *(const WORD*)(Block + 2);
	DWORD Bits = *(const DWORD*)(Block + 4);
	TireRGB Pal[4];

	FourColour = !Dxt1 || c0 > c1;
	Opaque = true;

	if (FourColour) Tire_Palette4(c0, c1, Pal);
	else
	{
		Pal[0] = Tire_Expand565(c0);
		Pal[1] = Tire_Expand565(c1);
		Pal[2] = { (Pal[0].r + Pal[1].r) / 2, (Pal[0].g + Pal[1].g) / 2, (Pal[0].b + Pal[1].b) / 2 };
		Pal[3] = { 0, 0, 0 };
	}

	for (int i = 0; i < 16; i++)
	{
		Idx[i] = (Bits >> (2 * i)) & 3;
		Px[i] = Pal[Idx[i]];

		if (!FourColour && Idx[i] == 3) Opaque = false;
	}
}

// The nearest of the four colours for every texel, and the total squared error.
static int Tire_PickIndices(const TireRGB Px[16], WORD c0, WORD c1, int Idx[16])
{
	TireRGB Pal[4];
	Tire_Palette4(c0, c1, Pal);

	int Error = 0;

	for (int i = 0; i < 16; i++)
	{
		int Best = 0, BestDistance = Tire_Distance(Px[i], Pal[0]);

		for (int k = 1; k < 4; k++)
		{
			int d = Tire_Distance(Px[i], Pal[k]);
			if (d < BestDistance) { Best = k; BestDistance = d; }
		}

		Idx[i] = Best;
		Error += BestDistance;
	}

	return Error;
}

// Moves both endpoints to where the chosen indices want them, least squares, and keeps the move
// while it helps.
static void Tire_Refine(const TireRGB Px[16], WORD& c0, WORD& c1, int Idx[16], int& Error)
{
	static const double Weight0[4] = { 1.0, 0.0, 2.0 / 3.0, 1.0 / 3.0 };

	for (int Pass = 0; Pass < 2; Pass++)
	{
		double A = 0, B = 0, C = 0, X0[3] = { 0, 0, 0 }, X1[3] = { 0, 0, 0 };

		for (int i = 0; i < 16; i++)
		{
			double w0 = Weight0[Idx[i]], w1 = 1.0 - w0;
			double p[3] = { (double)Px[i].r, (double)Px[i].g, (double)Px[i].b };

			A += w0 * w0; B += w0 * w1; C += w1 * w1;

			for (int k = 0; k < 3; k++) { X0[k] += w0 * p[k]; X1[k] += w1 * p[k]; }
		}

		double Det = A * C - B * B;

		if (Det < 1e-6 && Det > -1e-6) return;

		double E0[3], E1[3];

		for (int k = 0; k < 3; k++)
		{
			E0[k] = (C * X0[k] - B * X1[k]) / Det;
			E1[k] = (A * X1[k] - B * X0[k]) / Det;
		}

		WORD n0 = Tire_Pack565(E0), n1 = Tire_Pack565(E1);
		int NewIdx[16];
		int NewError = Tire_PickIndices(Px, n0, n1, NewIdx);

		if (NewError >= Error) return;

		c0 = n0; c1 = n1; Error = NewError;
		memcpy(Idx, NewIdx, sizeof(NewIdx));
	}
}

// A fresh fit: the endpoints at either end of the texels' spread along their principal axis.
static int Tire_FitColour(const TireRGB Px[16], WORD& c0, WORD& c1, int Idx[16])
{
	double Mean[3] = { 0, 0, 0 };

	for (int i = 0; i < 16; i++) { Mean[0] += Px[i].r; Mean[1] += Px[i].g; Mean[2] += Px[i].b; }
	for (int k = 0; k < 3; k++) Mean[k] /= 16.0;

	double Cov[3][3] = {};

	for (int i = 0; i < 16; i++)
	{
		double d[3] = { Px[i].r - Mean[0], Px[i].g - Mean[1], Px[i].b - Mean[2] };

		for (int a = 0; a < 3; a++)
			for (int b = 0; b < 3; b++)
				Cov[a][b] += d[a] * d[b];
	}

	// Power iteration from the covariance column with the most in it, which cannot be orthogonal to
	// the principal axis the way a fixed start like (1,1,1) can.
	int Start = 0;

	for (int k = 1; k < 3; k++)
		if (Cov[k][k] > Cov[Start][Start]) Start = k;

	double Axis[3] = { Cov[0][Start], Cov[1][Start], Cov[2][Start] };

	for (int Iteration = 0; Iteration < 8; Iteration++)
	{
		double v[3];
		double Length = 0;

		for (int a = 0; a < 3; a++)
		{
			v[a] = Cov[a][0] * Axis[0] + Cov[a][1] * Axis[1] + Cov[a][2] * Axis[2];
			Length = Length > (v[a] < 0 ? -v[a] : v[a]) ? Length : (v[a] < 0 ? -v[a] : v[a]);
		}

		if (Length < 1e-9) break;

		for (int a = 0; a < 3; a++) Axis[a] = v[a] / Length;
	}

	double Norm = Axis[0] * Axis[0] + Axis[1] * Axis[1] + Axis[2] * Axis[2];

	if (Norm < 1e-12)
	{
		// One colour throughout
		c0 = c1 = Tire_Pack565(Mean);
		return Tire_PickIndices(Px, c0, c1, Idx);
	}

	double Min = 1e30, Max = -1e30;

	for (int i = 0; i < 16; i++)
	{
		double t = ((Px[i].r - Mean[0]) * Axis[0] + (Px[i].g - Mean[1]) * Axis[1] + (Px[i].b - Mean[2]) * Axis[2]) / Norm;

		if (t < Min) Min = t;
		if (t > Max) Max = t;
	}

	double E0[3], E1[3];

	for (int k = 0; k < 3; k++)
	{
		E0[k] = Mean[k] + Axis[k] * Max;
		E1[k] = Mean[k] + Axis[k] * Min;
	}

	c0 = Tire_Pack565(E0);
	c1 = Tire_Pack565(E1);

	int Error = Tire_PickIndices(Px, c0, c1, Idx);

	Tire_Refine(Px, c0, c1, Idx, Error);

	return Error;
}

// Writes a four colour block. The larger endpoint has to come first for DXT1 to read four colours
// rather than three and a transparent one, so the ends are swapped where needed, which swaps index
// 0 with 1 and 2 with 3. Two equal ends leave nothing to choose between and take index 0 throughout.
static void Tire_WriteColour(BYTE* Block, WORD c0, WORD c1, const int Idx[16])
{
	bool Swap = c0 < c1;

	if (Swap) { WORD t = c0; c0 = c1; c1 = t; }

	DWORD Bits = 0;

	if (c0 != c1)
		for (int i = 0; i < 16; i++)
			Bits |= (DWORD)(Swap ? Idx[i] ^ 1 : Idx[i]) << (2 * i);

	*(WORD*)Block = c0;
	*(WORD*)(Block + 2) = c1;
	*(DWORD*)(Block + 4) = Bits;
}

// The mask's top level as one byte of coverage per texel, its brightest channel.
struct TireCoverage
{
	int Width = 0, Height = 0;
	std::vector<BYTE> Map;

	// A texel of a W by H level takes the average over the part of the mask it covers.
	int At(int x, int y, int W, int H) const
	{
		if (x >= W || y >= H) return 0;

		int x0 = x * Width / W, x1 = (x + 1) * Width / W;
		int y0 = y * Height / H, y1 = (y + 1) * Height / H;

		if (x1 <= x0) x1 = x0 + 1;
		if (y1 <= y0) y1 = y0 + 1;
		if (x1 > Width) x1 = Width;
		if (y1 > Height) y1 = Height;

		int Sum = 0;

		for (int yy = y0; yy < y1; yy++)
			for (int xx = x0; xx < x1; xx++)
				Sum += Map[(size_t)yy * Width + xx];

		return Sum / ((x1 - x0) * (y1 - y0));
	}
};

static inline BYTE Tire_Brightest(int r, int g, int b)
{
	return (BYTE)(r > g ? (r > b ? r : b) : (g > b ? g : b));
}

// Read only, so D3D does not count the mask as changed and send it to the card again.
bool Tire_ReadCoverage(void* Mask, const TireLevel& L, TireCoverage& Cover, long& Result)
{
	TireLockedRect Rect;

	if (!Tire_Lock(Mask, 0, L.RowBytes, Rect, Result, TIRE_D3DLOCK_READONLY)) return false;

	Cover.Width = L.Width;
	Cover.Height = L.Height;
	Cover.Map.assign((size_t)L.Width * L.Height, 0);

	if (L.Kind == TIRE_KIND_32BIT)
	{
		for (int y = 0; y < L.Height; y++)
		{
			const DWORD* Row = (const DWORD*)(Rect.Bits + (size_t)y * Rect.Pitch);

			for (int x = 0; x < L.Width; x++)
				Cover.Map[(size_t)y * L.Width + x] = Tire_Brightest((Row[x] >> 16) & 0xFF, (Row[x] >> 8) & 0xFF, Row[x] & 0xFF);
		}
	}
	else
	{
		int ColourOffset = L.Kind == TIRE_KIND_DXT1 ? 0 : 8;
		int BlocksX = L.RowBytes / L.BlockBytes;

		for (int by = 0; by < L.Rows; by++)
			for (int bx = 0; bx < BlocksX; bx++)
			{
				TireRGB Px[16];
				int Idx[16];
				bool FourColour, Opaque;

				Tire_DecodeColour(Rect.Bits + (size_t)by * Rect.Pitch + bx * L.BlockBytes + ColourOffset,
					L.Kind == TIRE_KIND_DXT1, Px, Idx, FourColour, Opaque);

				for (int i = 0; i < 16; i++)
				{
					int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);

					if (x < L.Width && y < L.Height)
						Cover.Map[(size_t)y * L.Width + x] = Tire_Brightest(Px[i].r, Px[i].g, Px[i].b);
				}
			}
	}

	Tire_Unlock(Mask, 0);

	return true;
}

struct TirePristine
{
	void* Texture;
	DWORD Hash;
	std::vector<TireLevel> Layout;
	std::vector<std::vector<BYTE>> Levels; // each level's rows, tightly packed
};

std::vector<TirePristine> TirePristineCopies;

void* TireLastPaintedTexture = nullptr;
DWORD* TireLastPaintPart = nullptr;

// Every level as it came out of the pack, taken the first time the texture is seen.
TirePristine* Tire_Pristine(void* Tex, DWORD Hash, long& Result, int& FailedLevel)
{
	int Levels = Tire_LevelCount(Tex);

	for (auto& C : TirePristineCopies)
		if (C.Texture == Tex && C.Hash == Hash && (int)C.Layout.size() == Levels) return &C;

	TirePristine Copy = { Tex, Hash };

	for (int Level = 0; Level < Levels; Level++)
	{
		TireLevel L;
		TireLockedRect Rect;

		FailedLevel = Level;
		Result = 0;

		if (!Tire_LevelLayout(Tex, Level, L) || L.Kind == TIRE_KIND_NONE) return nullptr;
		if (!Tire_Lock(Tex, Level, L.RowBytes, Rect, Result, TIRE_D3DLOCK_READONLY)) return nullptr;

		std::vector<BYTE> Bytes((size_t)L.Rows * L.RowBytes);

		for (int y = 0; y < L.Rows; y++)
			memcpy(&Bytes[(size_t)y * L.RowBytes], Rect.Bits + (size_t)y * Rect.Pitch, L.RowBytes);

		Tire_Unlock(Tex, Level);

		Copy.Layout.push_back(L);
		Copy.Levels.push_back(std::move(Bytes));
	}

	TirePristineCopies.push_back(std::move(Copy));

	return &TirePristineCopies.back();
}

struct TirePaintStats { int Covered, CoveredRed, Recoded, KeptTransparent; };

// Paints one level from its pristine bytes into the locked level.
void Tire_PaintLevel(const TireLevel& L, const BYTE* Src, TireLockedRect& Rect, const TireCoverage& Cover,
	int CR, int CG, int CB, bool TopLevel, TirePaintStats& Stats)
{
	if (L.Kind == TIRE_KIND_32BIT)
	{
		for (int y = 0; y < L.Height; y++)
		{
			const DWORD* In = (const DWORD*)(Src + (size_t)y * L.RowBytes);
			DWORD* Out = (DWORD*)(Rect.Bits + (size_t)y * Rect.Pitch);

			for (int x = 0; x < L.Width; x++)
			{
				DWORD P = In[x];
				int c = Cover.At(x, y, L.Width, L.Height);

				if (!c) { Out[x] = P; continue; }

				// A8R8G8B8 in memory: blue, green, red, alpha
				TireRGB p = { (int)(P >> 16) & 0xFF, (int)(P >> 8) & 0xFF, (int)P & 0xFF };
				TireRGB n = Tire_Mix(p, c, CR, CG, CB);

				if (TopLevel)
				{
					Stats.Covered++;
					if (p.r > p.g + 16 && p.r > p.b + 16) Stats.CoveredRed++;
				}

				Out[x] = (P & 0xFF000000) | ((DWORD)n.r << 16) | ((DWORD)n.g << 8) | (DWORD)n.b;
			}
		}

		return;
	}

	bool Dxt1 = L.Kind == TIRE_KIND_DXT1;
	int ColourOffset = Dxt1 ? 0 : 8;
	int BlocksX = L.RowBytes / L.BlockBytes;

	for (int by = 0; by < L.Rows; by++)
	{
		for (int bx = 0; bx < BlocksX; bx++)
		{
			const BYTE* In = Src + (size_t)by * L.RowBytes + bx * L.BlockBytes;
			BYTE* Out = Rect.Bits + (size_t)by * Rect.Pitch + bx * L.BlockBytes;

			// Back to the authored block first, alpha half included, so anything not painted below
			// is exactly what the pack held.
			memcpy(Out, In, L.BlockBytes);

			TireRGB Px[16], Target[16];
			int OwnIdx[16], Covers[16];
			bool FourColour, Opaque;

			Tire_DecodeColour(In + ColourOffset, Dxt1, Px, OwnIdx, FourColour, Opaque);

			int CoverSum = 0;
			bool Changed = false;

			for (int i = 0; i < 16; i++)
			{
				int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);

				Covers[i] = Cover.At(x, y, L.Width, L.Height);
				Target[i] = Tire_Mix(Px[i], Covers[i], CR, CG, CB);
				CoverSum += Covers[i];

				if (!(Target[i] == Px[i])) Changed = true;

				if (TopLevel && Covers[i] && x < L.Width && y < L.Height)
				{
					Stats.Covered++;
					if (Px[i].r > Px[i].g + 16 && Px[i].r > Px[i].b + 16) Stats.CoveredRed++;
				}
			}

			if (!Changed) continue;

			if (!Opaque)
			{
				Stats.KeptTransparent++;
				continue;
			}

			// The block's own ends through the mix, at the block's average coverage
			WORD Best0 = 0, Best1 = 0;
			int BestIdx[16];
			int BestError = 0x7FFFFFFF;

			if (FourColour)
			{
				int c = CoverSum / 16;
				TireRGB E0 = Tire_Mix(Tire_Expand565(*(const WORD*)(In + ColourOffset)), c, CR, CG, CB);
				TireRGB E1 = Tire_Mix(Tire_Expand565(*(const WORD*)(In + ColourOffset + 2)), c, CR, CG, CB);

				Best0 = Tire_Pack565(E0.r, E0.g, E0.b);
				Best1 = Tire_Pack565(E1.r, E1.g, E1.b);
				BestError = Tire_PickIndices(Target, Best0, Best1, BestIdx);

				Tire_Refine(Target, Best0, Best1, BestIdx, BestError);
			}

			// A fresh fit, for when the mask does not cover the block evenly
			if (BestError)
			{
				WORD f0, f1;
				int FitIdx[16];
				int FitError = Tire_FitColour(Target, f0, f1, FitIdx);

				if (FitError < BestError)
				{
					Best0 = f0; Best1 = f1; BestError = FitError;
					memcpy(BestIdx, FitIdx, sizeof(FitIdx));
				}
			}

			Tire_WriteColour(Out + ColourOffset, Best0, Best1, BestIdx);
			Stats.Recoded++;
		}
	}
}

void Tire_PaintIfWanted(DWORD* RideInfo, DWORD TextureHash)
{
	if (!TextureHash || !Tire_ValidPtr(RideInfo)) return;

	DWORD* Part = (DWORD*)RideInfo[356 + TIRE_CAR_SLOT];

	if (!Part) return;

	if (!Tire_ValidPartPtr(Part))
	{
		TireNote("tyre part pointer %p is not a usable address, left as it is\n", Part);
		return;
	}

	if (!CarPart_GetAppliedAttributeUParam(Part, TIRE_ATTR_PAINTABLE, 0))
	{
		TireNote("tyre part %08X, texture %08X: no PAINTABLE attribute, left as it is\n",
			(unsigned int)Part[0], (unsigned int)TextureHash);
		return;
	}

	DWORD MaskHash = bStringHash2("_MASK", TextureHash);
	void* TexInfo = GetTextureInfo(TextureHash, 0, 0);
	void* MaskInfo = GetTextureInfo(MaskHash, 0, 0);

	if (!TexInfo || !MaskInfo)
	{
		TireNote("paintable tyre %08X: texture %s, mask %08X %s in any loaded pack, nothing to paint\n",
			(unsigned int)TextureHash, TexInfo ? "found" : "NOT found",
			(unsigned int)MaskHash, MaskInfo ? "found" : "NOT found");
		return;
	}

	void* Tex = Tire_D3DTexture(TexInfo);
	void* Mask = Tire_D3DTexture(MaskInfo);

	if (!Tex || !Mask)
	{
		TireNote("paintable tyre %08X skipped: no D3D texture behind it (texture %p, mask %p)\n",
			(unsigned int)TextureHash, Tex, Mask);
		return;
	}

	DWORD* ColourPart = (DWORD*)RideInfo[356 + CARSLOTID_WHEEL_MANUFACTURER];

	// Only redo it when something it depends on has changed. The texture is part of that: a pack
	// unloaded and loaded again hands back fresh pixels in a new texture.
	if (Tex == TireLastPaintedTexture && ColourPart == TireLastPaintPart) return;

	TireLevel TexTop, MaskTop;
	char TexFormat[16], MaskFormat[16];

	if (!Tire_LevelLayout(Tex, 0, TexTop) || !Tire_LevelLayout(Mask, 0, MaskTop))
	{
		TireNote("paintable tyre %08X skipped: D3D would not describe the texture or the mask\n", (unsigned int)TextureHash);
		return;
	}

	int Levels = Tire_LevelCount(Tex);

	TireNote("paintable tyre %08X: texture %s %dx%d with %d level(s), mask %08X %s %dx%d\n",
		(unsigned int)TextureHash, Tire_FormatName(TexTop.Format, TexFormat), TexTop.Width, TexTop.Height, Levels,
		(unsigned int)MaskHash, Tire_FormatName(MaskTop.Format, MaskFormat), MaskTop.Width, MaskTop.Height);

	if (TexTop.Kind == TIRE_KIND_NONE || MaskTop.Kind == TIRE_KIND_NONE)
	{
		TireNote("paintable tyre %08X skipped: %s is not a format this paints. Use A8R8G8B8, X8R8G8B8, DXT1,"
			" DXT3 or DXT5\n", (unsigned int)TextureHash,
			TexTop.Kind == TIRE_KIND_NONE ? TexFormat : MaskFormat);
		return;
	}

	long Result = 0;
	int FailedLevel = 0;
	TirePristine* Copy = Tire_Pristine(Tex, TextureHash, Result, FailedLevel);

	if (!Copy)
	{
		TireNote("paintable tyre %08X skipped: level %d could not be read, LockRect returned %08X\n",
			(unsigned int)TextureHash, FailedLevel, (unsigned int)Result);
		return;
	}

	TireCoverage Cover;

	if (!Tire_ReadCoverage(Mask, MaskTop, Cover, Result))
	{
		TireNote("paintable tyre %08X skipped: mask LockRect returned %08X\n",
			(unsigned int)TextureHash, (unsigned int)Result);
		return;
	}

	// No colour, or the unused first entry, is the vinyl identity: red stays red.
	int CR = 255, CG = 0, CB = 0;
	bool HasColour = Tire_ValidPartPtr(ColourPart) && ColourPart[0] != CT_bStringHash("VINYL_L1_COLOR01");

	if (HasColour)
	{
		CR = TireClamp(CarPart_GetAppliedAttributeUParam(ColourPart, CT_bStringHash("RED"), 0));
		CG = TireClamp(CarPart_GetAppliedAttributeUParam(ColourPart, CT_bStringHash("GREEN"), 0));
		CB = TireClamp(CarPart_GetAppliedAttributeUParam(ColourPart, CT_bStringHash("BLUE"), 0));
	}

	TirePaintStats Stats = {};
	int Painted = 0;

	for (int Level = 0; Level < (int)Copy->Layout.size(); Level++)
	{
		const TireLevel& L = Copy->Layout[Level];
		TireLockedRect Rect;

		if (!Tire_Lock(Tex, Level, L.RowBytes, Rect, Result, 0))
		{
			TireNote("paintable tyre %08X: level %d LockRect returned %08X, that level keeps what it had\n",
				(unsigned int)TextureHash, Level, (unsigned int)Result);
			continue;
		}

		Tire_PaintLevel(L, Copy->Levels[Level].data(), Rect, Cover, CR, CG, CB, Level == 0, Stats);
		Tire_Unlock(Tex, Level);
		Painted++;
	}

	TireLastPaintedTexture = Tex;
	TireLastPaintPart = ColourPart;

	// Said every time the colour changes, since the colour is in the line. Covered 0 means the mask
	// is black where it should not be; covered but no red means the stripe was not authored in red.
	TireNote("painted tyre %08X with %s %d,%d,%d: %d of %d texels under the mask, %d of those red,"
		" %d of %d level(s), %d DXT block(s) re-encoded\n",
		(unsigned int)TextureHash, HasColour ? "colour" : "no colour, identity", CR, CG, CB,
		Stats.Covered, TexTop.Width * TexTop.Height, Stats.CoveredRed, Painted, (int)Copy->Layout.size(), Stats.Recoded);

	if (Stats.KeptTransparent)
		TireNote("paintable tyre %08X: %d DXT1 block(s) use the transparent texel and were left unpainted."
			" Save it as DXT5, or as DXT1 without alpha.\n", (unsigned int)TextureHash, Stats.KeptTransparent);
}

// Once per car per frame, off the front of CarRenderInfo::Render and RenderFast. Resolving here
// rather than per mesh keeps the lookup out of the inner loop, and resolving every frame rather
// than caching keeps the pointer honest: a TextureInfo belongs to a pack and packs come and go.
void __cdecl Tire_SetCurrentCar(DWORD* CarRenderInfo)
{
	DWORD* RideInfo = Tire_ValidPtr(CarRenderInfo) ? (DWORD*)CarRenderInfo[1] : nullptr;
	DWORD Hash = RideInfo ? Tire_TextureHashForRide(RideInfo) : 0;

	Tire_PaintIfWanted(RideInfo, Hash);

	// 0 as the second argument, so a texture no pack holds comes back null and the tyre keeps its
	// own texture. With 1 the game substitutes DefaultTextureInfo (0x4902AF), which would put the
	// missing texture placeholder on the tyre and, being never null, kept the not loaded note below
	// from ever firing.
	TireCurrentTexture = Hash ? GetTextureInfo(Hash, 0, 0) : nullptr;
	TireHookLive = (TireCurrentTexture || TireMaterialProbe) ? 1 : 0;

	if (!RideInfo || TireTextureOverride) return;

	DWORD* Part = (DWORD*)RideInfo[356 + TIRE_CAR_SLOT];

	// Each is said once per car type rather than once for the whole game, so a preview with no tyre
	// part cannot use up the line for the car actually being looked at.
	if (!Part)
		TireNote("car type %d: no tyre part in slot %d\n", RideInfo[0], TIRE_CAR_SLOT);
	else if (!Hash)
		TireNote("car type %d: tyre part %08X carries no TEXTURE_NAME, so it names no texture\n",
			RideInfo[0], (unsigned int)Part[0]);
	else if (!TireCurrentTexture)
		TireNote("car type %d: tyre texture %08X is in no loaded pack, the tyre keeps its own. It has"
			" to reach the car the way a vinyl or a rim texture does.\n", RideInfo[0], (unsigned int)Hash);
	else
		TireNote("car type %d: tyre part %08X draws texture %08X\n", RideInfo[0], (unsigned int)Part[0], (unsigned int)Hash);
}

// Runs for every mesh the game draws while a tyre texture is live, so it gets out of the way as
// early as it can. The renderer has already worked out this mesh's own TextureInfo; all this
// decides is whether to hand back a different one.
void* __cdecl Tire_ResolveMeshTexture(DWORD* Solid, BYTE* Mesh, void* Default)
{
	BYTE Index = eMesh_LightMaterialIndex(Mesh);
	if (Index == 0xFF) return Default;

	int Count = eSolid_NumLightMaterials(Solid);
	DWORD* Table = eSolid_LightMaterials(Solid);

	if (Count <= 0 || Index >= Count || !Tire_ValidPtr(Table)) return Default;
	if (Table[Index * 2] != TIRE_MATERIAL_RUBBER) return Default;

	Tire_ProbeSolid(Solid);

	return TireCurrentTexture ? TireCurrentTexture : Default;
}

// Replaces the two instructions that fetch a mesh's TextureInfo. ecx, edx and ebp are all
// reassigned before the renderer reads them again, so only eax has to survive, and it does.
static constexpr DWORD TireMeshHookReturn = 0x005C5A9B;

__declspec(naked) void Tire_MeshTextureHook()
{
	__asm
	{
		mov ecx, [eax + 0x2C];
		mov ebp, [ecx + edx * 8 + 4];

		mov ecx, TireHookLive;
		test ecx, ecx;
		je keep;

		push eax;
		push edx;

		push ebp;		// the texture this mesh would have drawn with
		push esi;		// the mesh entry
		push eax;		// the eSolid
		call Tire_ResolveMeshTexture;
		add esp, 0x0C;

		mov ebp, eax;

		pop edx;
		pop eax;

	keep:
		jmp TireMeshHookReturn;
	}
}

// Both render entries open with the same six bytes, push ebp / mov ebp, esp / and esp, -16, which
// the hooks replay after picking the car's tyre texture. ecx is the CarRenderInfo.
static constexpr DWORD TireRenderFastReturn = 0x006162E6;
static constexpr DWORD TireRenderReturn = 0x00620AE6;

__declspec(naked) void Tire_RenderFastHook()
{
	__asm
	{
		pushad;
		push ecx;
		call Tire_SetCurrentCar;
		add esp, 4;
		popad;

		push ebp;
		mov ebp, esp;
		and esp, 0xFFFFFFF0;
		jmp TireRenderFastReturn;
	}
}

__declspec(naked) void Tire_RenderHook()
{
	__asm
	{
		pushad;
		push ecx;
		call Tire_SetCurrentCar;
		add esp, 4;
		popad;

		push ebp;
		mov ebp, esp;
		and esp, 0xFFFFFFF0;
		jmp TireRenderReturn;
	}
}

// Called from LoaderCarInfo_Hook once the car part type name table is in memory. Entry [0] is left
// alone so a car's own part in this slot still wins; only the second name, the one the browser
// continues into, becomes the tyre collection.
void Tire_OfferPartsToEveryCar()
{
	if (!TirePartsCollection) return;

	DWORD* Table = *(DWORD**)_DefaultSlotTypeNameTable;
	if (!Tire_ValidPtr(Table)) return;

	DWORD* Entry = &Table[TIRE_CAR_SLOT * 2];

	// 0xFFFFFFFF means "the car's own type", which entry [0] already says, so it is free to take.
	if (Entry[1] == 0xFFFFFFFF || Entry[1] == 0)
	{
		Entry[1] = TirePartsCollection;
	}
	else
	{
		TireProbeLine("slot %d already searches car type 0x%08X as its second name, so the tyre"
			" collection was not added. Point TIRE_CAR_SLOT at a free slot.\n",
			TIRE_CAR_SLOT, (unsigned int)Entry[1]);
	}
}

void InitTireMaterial()
{
	TireHookLive = TireMaterialProbe ? 1 : 0;

	if (!TirePartsCollection && !TireTextureOverride && !TireMaterialProbe) return;

	// eViewPlatInterface::Render, the per mesh texture fetch. Seven bytes are replaced:
	// mov ecx, [eax+2Ch] (3) and mov ebp, [ecx+edx*8+4] (4). The hook does both itself and returns
	// past them, so the two bytes after the jump are unreachable.
	injector::MakeJMP(0x005C5A94, Tire_MeshTextureHook, true);

	// CarRenderInfo::RenderFast and CarRenderInfo::Render, so the texture follows the car being
	// drawn rather than being one global for the whole game.
	injector::MakeJMP(0x006162E0, Tire_RenderFastHook, true);
	injector::MakeNOP(0x006162E5, 1, true);

	injector::MakeJMP(0x00620AE0, Tire_RenderHook, true);
	injector::MakeNOP(0x00620AE5, 1, true);
}
