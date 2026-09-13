#pragma once

#include "stdio.h"
#include "InGameFunctions.h"
#include "GlobalVariables.h"
#include "includes\injector\injector.hpp"

// ---------------------------------------------------------------------------------------------
// Tyre material probe
//
// Idea being tested: tyres are the one thing wearing the material RUBBER, and their UVs are the
// same on every rim, so if that material's appearance can be driven from a part the way carbon
// and rim colour already are, tyres become customisable. Toyo lettering, a cut Bridgestone, and
// so on.
//
// What is already known, from the geometry and from the disassembly:
//
//   RUBBER is bStringHash "RUBBER" = 0x78743CA1, and it is in every GEOMETRY_<brand>.BIN under
//   CARS\WHEELS next to MAGSILVER, MAGCHROME and MAGGUNMETAL. It is also in every car's own
//   GEOMETRY.BIN, 30 to 55 times, which is the stock wheels rather than anything else, so
//   "tyres only" holds in spirit but not literally.
//
//   CarRenderInfo::Render already does exactly the call this feature needs, three times, on the
//   wheel model at 0x6277B1, 0x6277D1 and 0x6277F1:
//
//     eModel::ReplaceLightMaterial(0x22719FA9 MAGSILVER, <the rim colour material>)
//
//   and that function, at 0x48D860, walks a table hanging off the solid:
//
//     eModel  +0x0C -> eSolid
//     eSolid  +0x1A  byte, how many entries
//     eSolid  +0x3C  -> entries of 8 bytes: { DWORD NameHash; eLightMaterial* Material; }
//
//   so aiming one more of those at RUBBER is a single call in a place that already exists.
//
// What is NOT known, and is the whole question: whether an eLightMaterial carries a texture, or
// only shading. If it carries one, this works as described. If it does not, the texture has to
// come from the ReplacementTextureEntry table instead, which the fork already has helpers for,
// and which is keyed by the OLD texture hash and so needs that hash discovered per rim first.
//
// Guessing at that would cost a build either way, so this file does not implement the feature. It
// answers the question:
//
//   [Debug] TireMaterialProbe = 1
//     dumps each model's light material table once to UnlimiterData\_TireProbe.txt, which says
//     whether RUBBER is really there at runtime and what it is bound to.
//
//   [Debug] TireMaterialSwapFrom = MAGCHROME
//     points RUBBER at the material some other name on the SAME model is already using. No new
//     assets, nothing to author. If the tyres turn chrome, a light material decides how a tyre
//     looks and the feature is a part attribute away. If they do not change at all, the answer
//     is the texture table and this whole approach is the wrong one.
// ---------------------------------------------------------------------------------------------

bool TireMaterialProbe = false;
DWORD TireMaterialSwapFrom = 0;

#define TIRE_MATERIAL_RUBBER CT_bStringHash("RUBBER")

// eSolid, reached through the model, and its light material table
#define eModel_Solid(model)          (*(DWORD**)((BYTE*)(model) + 0x0C))
#define eSolid_NumLightMaterials(s)  (*(char*)((BYTE*)(s) + 0x1A))
#define eSolid_LightMaterials(s)     (*(DWORD**)((BYTE*)(s) + 0x3C))

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

// One line per model, not one per frame. Render calls this three times per wheel per frame, so
// without a seen list the file is hundreds of megabytes before anyone reads it.
std::vector<DWORD*> TireProbeSeen;

bool Tire_AlreadyProbed(DWORD* Model)
{
	for (size_t i = 0; i < TireProbeSeen.size(); i++)
		if (TireProbeSeen[i] == Model) return true;

	if (TireProbeSeen.size() < 64) TireProbeSeen.push_back(Model);

	return false;
}

std::vector<DWORD*> TireWarned;

bool Tire_AlreadyWarned(DWORD* Model)
{
	for (size_t i = 0; i < TireWarned.size(); i++)
		if (TireWarned[i] == Model) return true;

	if (TireWarned.size() < 64) TireWarned.push_back(Model);

	return false;
}

// The material some name on this model is currently bound to, which is how the experiment gets a
// real eLightMaterial without inventing one.
DWORD* Tire_MaterialBoundTo(DWORD* Model, DWORD NameHash)
{
	if (!Tire_ValidPtr(Model)) return nullptr;

	DWORD* Solid = eModel_Solid(Model);
	if (!Tire_ValidPtr(Solid)) return nullptr;

	int Count = eSolid_NumLightMaterials(Solid);
	DWORD* Entries = eSolid_LightMaterials(Solid);

	if (Count <= 0 || Count > 64 || !Tire_ValidPtr(Entries)) return nullptr;

	for (int i = 0; i < Count; i++)
		if (Entries[i * 2] == NameHash) return (DWORD*)Entries[i * 2 + 1];

	return nullptr;
}

void Tire_ProbeModel(DWORD* Model)
{
	if (!TireMaterialProbe || !Tire_ValidPtr(Model)) return;
	if (Tire_AlreadyProbed(Model)) return;

	DWORD* Solid = eModel_Solid(Model);

	if (!Tire_ValidPtr(Solid))
	{
		TireProbeLine("model %p: no solid\n", Model);
		return;
	}

	int Count = eSolid_NumLightMaterials(Solid);
	DWORD* Entries = eSolid_LightMaterials(Solid);

	TireProbeLine("model %p solid %p: %d light material(s) at %p\n", Model, Solid, Count, Entries);

	if (Count <= 0 || Count > 64 || !Tire_ValidPtr(Entries)) return;

	for (int i = 0; i < Count; i++)
	{
		DWORD Name = Entries[i * 2];
		DWORD* Material = (DWORD*)Entries[i * 2 + 1];

		char const* Known =
			Name == TIRE_MATERIAL_RUBBER      ? " RUBBER" :
			Name == CT_bStringHash("MAGSILVER")   ? " MAGSILVER" :
			Name == CT_bStringHash("MAGCHROME")   ? " MAGCHROME" :
			Name == CT_bStringHash("MAGGUNMETAL") ? " MAGGUNMETAL" :
			Name == CT_bStringHash("CARSKIN")     ? " CARSKIN" : "";

		TireProbeLine("    [%2d] 0x%08X%s -> %p\n", i, (unsigned int)Name, Known, Material);
	}
}

// Wraps the first of the three rim colour calls in the front wheel block, which is a place that
// already holds the wheel model and runs once per wheel per frame.
void __fastcall Tire_ReplaceLightMaterial(DWORD* Model, void* EDX_Unused, int NameHash, int Material)
{
	Tire_ProbeModel(Model);

	// The experiment. RUBBER is pointed at whatever material the named one on this same model is
	// using, so nothing has to be authored to find out whether a light material is what decides
	// how a tyre looks.
	if (TireMaterialSwapFrom)
	{
		DWORD* Source = Tire_MaterialBoundTo(Model, TireMaterialSwapFrom);

		if (Source) eModel_ReplaceLightMaterial_Game(Model, TIRE_MATERIAL_RUBBER, (int)Source);
		else if (!Tire_AlreadyWarned(Model))
			TireProbeLine("model %p: TireMaterialSwapFrom 0x%08X is not a material on this model,"
				" nothing to copy. The probe lines above list the names it does have.\n",
				Model, (unsigned int)TireMaterialSwapFrom);
	}

	eModel_ReplaceLightMaterial_Game(Model, NameHash, Material);
}

// THE CAR IS DRAWN BY RenderFast, NOT BY Render. The first cut of this hooked only the front
// wheel block inside CarRenderInfo::Render and the probe file was never even created, which
// was not evidence about light materials, only about the hook never running.
//
// There are four wheel blocks, each opening with the same push of MAGSILVER:
//
//     0x617979  RenderFast, front wheel
//     0x6179C6  RenderFast, rear wheel
//     0x6277B1  Render,     front wheel
//     0x62782E  Render,     rear wheel
//
// All four, because which one runs depends on the screen and being wrong about that is what
// wasted the first attempt.
void InitTireMaterial()
{
	if (!TireMaterialProbe && !TireMaterialSwapFrom) return;

	// Call sites, so the original stays callable and no prologue has to be replayed.
	injector::MakeCALL(0x617979, Tire_ReplaceLightMaterial, true); // RenderFast, front
	injector::MakeCALL(0x6179C6, Tire_ReplaceLightMaterial, true); // RenderFast, rear
	injector::MakeCALL(0x6277B1, Tire_ReplaceLightMaterial, true); // Render, front
	injector::MakeCALL(0x62782E, Tire_ReplaceLightMaterial, true); // Render, rear
}
