#pragma once

#include "stdio.h"
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

bool TireTextureMissingLogged = false;

// A paintable tyre takes its colour from the part in WHEEL_MANUFACTURER, which is the same part
// the tyre smoke reads RED, GREEN and BLUE from, so the tyre and the smoke are in step by
// construction rather than by anything here keeping them so. CompositeWheel is handed that slot and
// does the reading itself, at 0x61DF01 onwards.
//
// The mask is the texture's own name with _MASK after it. bStringHash is a fold, so continuing it
// with bStringHash2 appends to a name that is only known as a hash: bStringHash2("_MASK", h) is the
// hash of the original string with _MASK on the end. Confirmed against the authored data,
// TIRE_PAINTABLE hashing to A2BEB302 and TIRE_PAINTABLE_MASK to A07354CD.
//
// A tyre without the attribute, or without a mask, is left alone. CompositeWheel returns at
// 0x61DEAD and 0x61DEFB when a texture it was handed is not loaded, so a missing mask costs a
// lookup and nothing else.

DWORD TireLastPaintedTexture = 0;
DWORD* TireLastPaintPart = nullptr;

void Tire_PaintIfWanted(DWORD* RideInfo, DWORD TextureHash)
{
	if (!TextureHash || !Tire_ValidPtr(RideInfo)) return;

	DWORD* Part = (DWORD*)RideInfo[356 + TIRE_CAR_SLOT];

	if (!Tire_ValidPtr(Part)) return;
	if (!CarPart_GetAppliedAttributeUParam(Part, TIRE_ATTR_PAINTABLE, 0)) return;

	DWORD* PaintPart = (DWORD*)RideInfo[356 + CARSLOTID_WHEEL_MANUFACTURER];

	// Compositing writes pixels, so doing it once per car per frame would be paid every frame for
	// nothing. Only redo it when the tyre or the colour behind it has actually changed.
	if (TextureHash == TireLastPaintedTexture && PaintPart == TireLastPaintPart) return;

	TireLastPaintedTexture = TextureHash;
	TireLastPaintPart = PaintPart;

	CompositeWheel(RideInfo, TextureHash, TextureHash,
		bStringHash2("_MASK", TextureHash), CARSLOTID_WHEEL_MANUFACTURER);
}

// Once per car per frame, off the front of CarRenderInfo::Render and RenderFast. Resolving here
// rather than per mesh keeps the lookup out of the inner loop, and resolving every frame rather
// than caching keeps the pointer honest: a TextureInfo belongs to a pack and packs come and go.
void __cdecl Tire_SetCurrentCar(DWORD* CarRenderInfo)
{
	DWORD* RideInfo = Tire_ValidPtr(CarRenderInfo) ? (DWORD*)CarRenderInfo[1] : nullptr;
	DWORD Hash = RideInfo ? Tire_TextureHashForRide(RideInfo) : 0;

	Tire_PaintIfWanted(RideInfo, Hash);

	TireCurrentTexture = Hash ? GetTextureInfo(Hash, 1, 0) : nullptr;
	TireHookLive = (TireCurrentTexture || TireMaterialProbe) ? 1 : 0;

	if (Hash && !TireCurrentTexture && !TireTextureMissingLogged)
	{
		TireTextureMissingLogged = true;
		TireProbeLine("tyre texture 0x%08X is in no pack that is loaded right now, so the tyre keeps"
			" its own texture. The texture has to reach the car the way a vinyl or a rim texture"
			" does.\n", (unsigned int)Hash);
	}
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
