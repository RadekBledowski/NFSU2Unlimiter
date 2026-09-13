#pragma once

#include "stdio.h"
#include "InGameFunctions.h"
#include "GlobalVariables.h"
#include "includes\injector\injector.hpp"

// ---------------------------------------------------------------------------------------------
// Tyre texture, driven per material
//
// The goal: give the tyre its own texture (Toyo lettering, a cut Bridgestone) without touching
// the rim, on unmodified wheel geometry.
//
// WHAT THE GEOMETRY ACTUALLY SAYS. Dumping 5ZIGEN_STYLE01_15_23_A out of
// CARS\WHEELS\GEOMETRY_5ZIGEN.BIN:
//
//     textures (1):          [0] 0x733AE956
//     light materials (3):   [0] RUBBER  [1] 0x010CB64A  [2] MAGCHROME
//     materials (3):         tex 0 + RUBBER      120 tris   <- the tyre
//                            tex 0 + MAGCHROME   355 tris   <- the rim
//                            tex 0 + 0x010CB64A   22 tris
//
// One texture slot, three materials. Exactly what Radek said: rim and tyre share the texture.
// So the light material name is the ONLY thing separating the tyre from the rim, and any
// override has to key on it.
//
// WHY THE VANILLA REPLACEMENT TABLE CANNOT DO THIS. eModel::AttachReplacementTextureTable puts a
// table at eModel+0x10 (count at +0x14) and eViewPlatInterface::Render applies it through
// sub_4905A0, which walks the SOLID's texture slots and swaps a slot whose hash matches an
// entry's old hash. Keyed by texture hash, applied to the slot. With one shared slot it would
// repaint the rim along with the tyre. That is the route this file deliberately does not take.
//
// WHERE THE SEPARATION IS. eViewPlatInterface::Render (0x5C5930) draws one mesh at a time, and
// at 0x5C5A8C it has both halves in hand at once:
//
//     movzx edx, byte ptr [esi+1Ch]      ; mesh entry +0x1C = texture index
//     mov   eax, [esp+10h]               ; eSolid
//     mov   ecx, [eax+2Ch]               ; the solid's texture table, entries { hash, TextureInfo* }
//     mov   ebp, [ecx+edx*8+4]           ; <- the TextureInfo this mesh will draw with
//     ...
//     mov   al, [esi+20h]                ; mesh entry +0x20 = light material index, 0xFF = none
//     mov   edx, [eax+3Ch]               ; the solid's light material table, { nameHash, eLightMaterial* }
//
// Substituting ebp there is a per material texture override, which is the thing the replacement
// table cannot express. Mesh entries are the file's 60 byte shading groups, loaded in place: the
// loop advances by 0x3C, count at [eSolid+0][0x10], array at [eSolid+0][0x14].
//
// THE FIRST ATTEMPT WAS MEASURED WRONG, twice over, and both mistakes are worth keeping written
// down because they cost a round trip each:
//
//   1. It hooked eModel::ReplaceLightMaterial inside CarRenderInfo::Render only, but the front
//      end draws through RenderFast. The probe file was never created. That was not evidence
//      about materials, only about a hook that never ran.
//
//   2. Even with all four wheel blocks hooked it would still have proved nothing on a stock car.
//      Every one of them sits inside "if (CarRenderInfo+0x390 != NULL)", and that slot is the rim
//      light material, which CarRenderInfo::CarRenderInfo fills from a part's LIGHT_MATERIAL_NAME
//      attribute (0x6BA02C05) and only for parts with upgrade level >= 1. Stock rims leave it
//      null and the whole block is skipped.
//
// UNVERIFIED: that a TextureInfo from an unrelated pack binds cleanly on a wheel mesh. It is the
// same pointer type the slot already holds and the same thing sub_4905A0 writes into that slot,
// so there is no reason for it not to, but it has not been seen on screen yet.
//
// SETTINGS. This first cut is deliberately global: every tyre in the game takes the same texture.
// Once it is confirmed on screen, the hash moves to a part attribute so a trim or a part can pick
// it per car, which is the same shape LIGHT_MATERIAL_NAME already has.
//
//   [Debug] TireTexture = CARBONFIBRE
//     the texture every RUBBER material draws with. CARBONFIBRE is a good first test because it
//     is loaded whenever a car is on screen, so nothing has to be authored to see the answer.
//
//   [Debug] TireMaterialProbe = 1
//     writes UnlimiterData\_TireProbe.txt: for each solid carrying a RUBBER material, its texture
//     slots and light material names, once per solid.
// ---------------------------------------------------------------------------------------------

bool TireMaterialProbe = false;
DWORD TireTextureHash = 0;

// The hook reads this one word to decide whether to bother. It is a DWORD because the naked shim
// tests it, and it covers the probe as well so the probe can run on its own with no texture set.
DWORD TireHookActive = 0;

#define TIRE_MATERIAL_RUBBER CT_bStringHash("RUBBER")

// eSolid, as the renderer reads it
#define eSolid_NumTextures(s)        (*(BYTE*)((BYTE*)(s) + 0x19))
#define eSolid_NumLightMaterials(s)  (*(char*)((BYTE*)(s) + 0x1A))
#define eSolid_TextureTable(s)       (*(DWORD**)((BYTE*)(s) + 0x2C))
#define eSolid_LightMaterials(s)     (*(DWORD**)((BYTE*)(s) + 0x3C))

// one 60 byte mesh entry, the runtime face of a shading group
#define eMesh_TextureIndex(m)        (*(BYTE*)((BYTE*)(m) + 0x1C))
#define eMesh_LightMaterialIndex(m)  (*(BYTE*)((BYTE*)(m) + 0x20))

void TireProbeLine(const char* fmt, ...)
{
	if (!TireMaterialProbe) return;

	auto Path = CurrentWorkingDirectory / "UnlimiterData" / "_TireProbe.txt";
	FILE* f = fopen(Path.string().c_str(), "a");
	if (!f) return;

	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);

	fclose(f);
}

bool Tire_ValidPtr(void* p)
{
	uintptr_t v = (uintptr_t)p;
	return v >= 0x00010000 && v <= 0xC0000000 && !(v & 3);
}

// One line per solid, not one per mesh per frame. This sits in the renderer's inner loop, so
// without a seen list the file is hundreds of megabytes before anyone opens it.
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

bool TireTextureMissingLogged = false;

// Runs for every mesh the game draws while a tyre texture is set, so it gets out of the way as
// early as it can. The renderer has already worked out the mesh's own TextureInfo; this only
// decides whether to hand back a different one.
void* __cdecl Tire_ResolveMeshTexture(DWORD* Solid, BYTE* Mesh, void* Default)
{
	BYTE Index = eMesh_LightMaterialIndex(Mesh);
	if (Index == 0xFF) return Default;

	int Count = eSolid_NumLightMaterials(Solid);
	DWORD* Table = eSolid_LightMaterials(Solid);

	if (Count <= 0 || Index >= Count || !Tire_ValidPtr(Table)) return Default;
	if (Table[Index * 2] != TIRE_MATERIAL_RUBBER) return Default;

	Tire_ProbeSolid(Solid);

	if (!TireTextureHash) return Default;

	// Resolved every time rather than cached, because a TextureInfo belongs to a pack and packs
	// come and go with the screen. This only runs for the one or two tyre meshes on a wheel, so
	// the lookup is not worth caching against a dangling pointer.
	void* Replacement = GetTextureInfo(TireTextureHash, 1, 0);

	if (!Replacement)
	{
		if (!TireTextureMissingLogged)
		{
			TireTextureMissingLogged = true;
			TireProbeLine("TireTexture 0x%08X is not in any pack that is loaded right now,"
				" so the tyre keeps its own texture.\n", (unsigned int)TireTextureHash);
		}
		return Default;
	}

	return Replacement;
}

// Replaces the two instructions that fetch a mesh's TextureInfo, then hands the result to the
// resolver. ecx, edx and ebp are all reassigned before the renderer reads them again, so only
// eax has to survive, and it does.
static constexpr DWORD TireMeshHookReturn = 0x005C5A9B;

__declspec(naked) void Tire_MeshTextureHook()
{
	__asm
	{
		mov ecx, [eax + 0x2C];
		mov ebp, [ecx + edx * 8 + 4];

		mov ecx, TireHookActive;
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

void InitTireMaterial()
{
	TireHookActive = (TireTextureHash || TireMaterialProbe) ? 1 : 0;
	if (!TireHookActive) return;

	// eViewPlatInterface::Render, the per mesh texture fetch. Seven bytes are replaced:
	// mov ecx, [eax+2Ch] (3) and mov ebp, [ecx+edx*8+4] (4). The hook does both itself and
	// returns past them, so the two bytes after the jump are simply unreachable.
	injector::MakeJMP(0x005C5A94, Tire_MeshTextureHook, true);
}
