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
// BOTH HAVE TO BE 32 BIT AND THE SAME SIZE. Only 32 bit texture memory is a plain array of pixels.
// The game creates those as D3DFMT_A8R8G8B8 in D3DPOOL_MANAGED (format 15h pushed to CreateTexture
// at 0x5CE2A4), so a pixel in memory is blue, green, red, alpha, with red in the third byte.
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
// throws away both the HRESULT and the row pitch; this needs the first to know the pointer is real
// and the second to walk rows correctly.
//
// Textures are looked up with GetTextureInfo(hash, 0, 0), which returns null for a texture no loaded
// pack holds. With 1 as the second argument it hands back DefaultTextureInfo instead (0x4902AF),
// which would be painted in place of the real thing.

#define TireTex_Format(t)  (*(BYTE*)((BYTE*)(t) + 0x4A))
#define TireTex_Width(t)   (*(short*)((BYTE*)(t) + 0x44))
#define TireTex_Height(t)  (*(short*)((BYTE*)(t) + 0x46))
#define TireTex_Mips(t)    (*(char*)((BYTE*)(t) + 0x4E))
#define TIRE_TEXTURE_32BIT 0x20

struct TireLockedRect { int Pitch; BYTE* Bits; };

void* Tire_D3DTexture(void* TextureInfo)
{
	if (!Tire_ValidPtr(TextureInfo)) return nullptr;

	BYTE* Plat = *(BYTE**)TextureInfo;

	if (!Tire_ValidPtr(Plat)) return nullptr;

	void* Texture = *(void**)(Plat + 0x18);

	return Tire_ValidPtr(Texture) ? Texture : nullptr;
}

bool Tire_Lock(void* Texture, TireLockedRect& Rect, long& Result)
{
	typedef long(__stdcall* LockRectFn)(void*, unsigned int, TireLockedRect*, const void*, unsigned long);

	Rect.Pitch = 0;
	Rect.Bits = nullptr;

	Result = ((LockRectFn)(*(void***)Texture)[0x4C / 4])(Texture, 0, &Rect, nullptr, 0);

	return Result >= 0 && Rect.Bits && Rect.Pitch > 0;
}

void Tire_Unlock(void* Texture)
{
	typedef long(__stdcall* UnlockRectFn)(void*, unsigned int);

	((UnlockRectFn)(*(void***)Texture)[0x50 / 4])(Texture, 0);
}

struct TirePristine
{
	void* TextureInfo;
	DWORD Hash;
	int Width, Height;
	std::vector<DWORD> Pixels; // tightly packed rows
};

std::vector<TirePristine> TirePristineCopies;

void* TireLastPaintedTexture = nullptr;
DWORD* TireLastPaintPart = nullptr;

static inline int TireClamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void Tire_PaintIfWanted(DWORD* RideInfo, DWORD TextureHash)
{
	if (!TextureHash || !Tire_ValidPtr(RideInfo)) return;

	DWORD* Part = (DWORD*)RideInfo[356 + TIRE_CAR_SLOT];

	if (!Tire_ValidPtr(Part)) return;

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

	TireNote("paintable tyre %08X: texture %p format %d %dx%d mips %d, mask %08X %p format %d %dx%d\n",
		(unsigned int)TextureHash, TexInfo, TireTex_Format(TexInfo), TireTex_Width(TexInfo),
		TireTex_Height(TexInfo), TireTex_Mips(TexInfo), (unsigned int)MaskHash, MaskInfo,
		TireTex_Format(MaskInfo), TireTex_Width(MaskInfo), TireTex_Height(MaskInfo));

	if (TireTex_Format(TexInfo) != TIRE_TEXTURE_32BIT || TireTex_Format(MaskInfo) != TIRE_TEXTURE_32BIT)
	{
		TireNote("paintable tyre %08X skipped: both have to be format 32, 32 bit\n", (unsigned int)TextureHash);
		return;
	}

	int W = TireTex_Width(TexInfo), H = TireTex_Height(TexInfo);

	if (W <= 0 || H <= 0 || W != TireTex_Width(MaskInfo) || H != TireTex_Height(MaskInfo))
	{
		TireNote("paintable tyre %08X skipped: texture and mask have to be the same size\n", (unsigned int)TextureHash);
		return;
	}

	DWORD* ColourPart = (DWORD*)RideInfo[356 + CARSLOTID_WHEEL_MANUFACTURER];

	// Only redo it when something it depends on has changed. The texture pointer is part of that:
	// a pack unloaded and loaded again hands back fresh pixels at a new address.
	if (TexInfo == TireLastPaintedTexture && ColourPart == TireLastPaintPart) return;

	void* Tex = Tire_D3DTexture(TexInfo);
	void* Mask = Tire_D3DTexture(MaskInfo);

	if (!Tex || !Mask)
	{
		TireNote("paintable tyre %08X skipped: no D3D texture behind it (texture %p, mask %p)\n",
			(unsigned int)TextureHash, Tex, Mask);
		return;
	}

	TireLockedRect TexRect, MaskRect;
	long TexResult = 0, MaskResult = 0;

	if (!Tire_Lock(Tex, TexRect, TexResult))
	{
		TireNote("paintable tyre %08X skipped: texture LockRect returned %08X\n",
			(unsigned int)TextureHash, (unsigned int)TexResult);
		return;
	}

	if (!Tire_Lock(Mask, MaskRect, MaskResult))
	{
		Tire_Unlock(Tex);
		TireNote("paintable tyre %08X skipped: mask LockRect returned %08X\n",
			(unsigned int)TextureHash, (unsigned int)MaskResult);
		return;
	}

	TirePristine* Copy = nullptr;

	for (auto& C : TirePristineCopies)
		if (C.TextureInfo == TexInfo && C.Hash == TextureHash && C.Width == W && C.Height == H) { Copy = &C; break; }

	if (!Copy)
	{
		TirePristineCopies.push_back({ TexInfo, TextureHash, W, H, std::vector<DWORD>((size_t)W * H) });
		Copy = &TirePristineCopies.back();

		for (int y = 0; y < H; y++)
			memcpy(&Copy->Pixels[(size_t)y * W], TexRect.Bits + (size_t)y * TexRect.Pitch, (size_t)W * 4);
	}

	// No colour, or the unused first entry, is the vinyl identity: red stays red.
	int CR = 255, CG = 0, CB = 0;
	bool HasColour = Tire_ValidPtr(ColourPart) && ColourPart[0] != CT_bStringHash("VINYL_L1_COLOR01");

	if (HasColour)
	{
		CR = TireClamp(CarPart_GetAppliedAttributeUParam(ColourPart, CT_bStringHash("RED"), 0));
		CG = TireClamp(CarPart_GetAppliedAttributeUParam(ColourPart, CT_bStringHash("GREEN"), 0));
		CB = TireClamp(CarPart_GetAppliedAttributeUParam(ColourPart, CT_bStringHash("BLUE"), 0));
	}

	int Covered = 0, CoveredRed = 0;

	for (int y = 0; y < H; y++)
	{
		const DWORD* Src = &Copy->Pixels[(size_t)y * W];
		DWORD* Dst = (DWORD*)(TexRect.Bits + (size_t)y * TexRect.Pitch);
		const DWORD* M = (const DWORD*)(MaskRect.Bits + (size_t)y * MaskRect.Pitch);

		for (int x = 0; x < W; x++)
		{
			DWORD P = Src[x];

			// A8R8G8B8 in memory: blue, green, red, alpha
			int b = P & 0xFF, g = (P >> 8) & 0xFF, r = (P >> 16) & 0xFF;

			DWORD MP = M[x];
			int mb = MP & 0xFF, mg = (MP >> 8) & 0xFF, mr = (MP >> 16) & 0xFF;
			int Cover = mr > mg ? (mr > mb ? mr : mb) : (mg > mb ? mg : mb); // greyscale, any channel

			if (!Cover) { Dst[x] = P; continue; }

			Covered++;
			if (r > g + 16 && r > b + 16) CoveredRed++;

			// The vinyl mix with only the first colour set: red becomes the colour, scaled by how red
			// the pixel was, and green and blue carry on as themselves.
			int nr = TireClamp(r * CR / 255);
			int ng = TireClamp(r * CG / 255 + g);
			int nb = TireClamp(r * CB / 255 + b);

			nr = r + (nr - r) * Cover / 255;
			ng = g + (ng - g) * Cover / 255;
			nb = b + (nb - b) * Cover / 255;

			Dst[x] = (P & 0xFF000000) | ((DWORD)nr << 16) | ((DWORD)ng << 8) | (DWORD)nb;
		}
	}

	Tire_Unlock(Mask);
	Tire_Unlock(Tex);

	TireLastPaintedTexture = TexInfo;
	TireLastPaintPart = ColourPart;

	// Said every time the colour changes, since the colour is in the line. Covered 0 means the mask
	// is black where it should not be; covered but no red means the stripe was not authored in red.
	TireNote("painted tyre %08X with %s %d,%d,%d: %d of %d pixels under the mask, %d of those red\n",
		(unsigned int)TextureHash, HasColour ? "colour" : "no colour, identity", CR, CG, CB,
		Covered, W * H, CoveredRed);

	if (TireTex_Mips(TexInfo) > 1)
		TireNote("paintable tyre %08X has %d mip levels and only the first is painted, so the stripe"
			" shows its authored red from further away. Author it with one level.\n",
			(unsigned int)TextureHash, TireTex_Mips(TexInfo));
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
