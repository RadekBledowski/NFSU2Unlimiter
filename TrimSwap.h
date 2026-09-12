#pragma once

#include "stdio.h"
#include "InGameFunctions.h"
#include "GlobalVariables.h"
#include "includes\injector\injector.hpp"

// ---------------------------------------------------------------------------------------------
// Car trims
//
// One car type, several factory versions. SUPRA and TURBOSUPRA are the same car slot as far as
// the car lot is concerned, but the badges, the name art, the base paint and the physics all come
// from whichever version is fitted.
//
// A trim is a Universal CarTypeInfo.
//
//   UsageType = Universal, which is 4. IsRacer/IsCop/IsTraffic compare against 0/1/2, so a
//   Universal entry falls out of all three: never spawned, never sold, never picked by AI. It
//   still gets its own <NAME>.ini for free, because LoadCarConfigs reads "<CollectionName>.ini"
//   for every car type in the table. That means a trim has the full per-car config, [BodyShop]
//   included, which is how a track only trim can restrict bodykits later on.
//
// The trim declares its parent in that ini:
//
//   [Main]
//   TrimOf = SUPRA
//
// Read into CarConfigs[].Main.TrimOf as a name hash. Never inherited from _General.ini.
//
// The selection is a part in RIGHT_SIDE_MIRROR, named <CAR>_<TRIM>:
//
//   SUPRA_SUPRA, SUPRA_TURBOSUPRA, SUPRA_BASESUPRA
//
// The name is read back with CarPart_GetName and the car prefix stripped, so the part needs no
// attributes at all. AttributeCount = 0 is fine.
//
// RIGHT_SIDE_MIRROR was chosen because LEFT_SIDE_MIRROR and RIGHT_SIDE_MIRROR appear in the whole
// tree only inside the carbon slot list; the mirrors a car actually wears live in WING_MIRROR as
// one part for both sides. CV (slot 167) was tried first and abandoned. WHEEL_MANUFACTURER (168)
// is spoken for, NLG plans burnt rubber colour there.
// ---------------------------------------------------------------------------------------------

int TrimSwapKey = 0;   // virtual key code, 0 turns it off
bool TrimTrace = false;

// CarTypeInfo fields a trim overrides. Stride is 0x890, layout taken from Nikki's serialisation
// order in Darius/Nikki/Support.Underground2/Class/CarTypeInfo.cs.
//
//   +0x0C0  ManufacturerName    accessor 0x610170, lea eax,[ecx+0C0h]
//   +0x110  physics block       480 floats, what RidePhysicsInfo::RebuildPhysicsInfo copies
//   +0x840  Index
//   +0x844  UsageType           Universal = 4
//   +0x84C  DefaultBasePaint    paint NAME hash, read by RideInfo::SetStockParts at 0x6370B6
//
// Cost at +0x87C is NOT in this list on purpose. Nothing in SPEED2.EXE ever reads that offset off
// a CarTypeInfo, cars in NFSU2 career are unlocked rather than bought, so making a trim cost
// something would have to invent the concept first.
#define CarTypeInfo_DefaultBasePaint(info) (*(DWORD*)((BYTE*)(info) + 0x84C))
#define CarTypeInfo_UsageType(info)        (*(BYTE*)((BYTE*)(info) + 0x844))

void TrimTraceLine(const char* fmt, ...)
{
	if (!TrimTrace) return;

	auto Path = CurrentWorkingDirectory / "UnlimiterData" / "_TrimSwap.txt";
	FILE* f = fopen(Path.string().c_str(), "a");
	if (!f) return;

	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);

	fclose(f);
}

// This reads hundreds of bytes past whatever it is given, so a small integer means a caller
// passed something that is not a pointer at all. Same shape as PartLink_ValidRideInfo.
bool Trim_ValidPtr(void* p)
{
	uintptr_t v = (uintptr_t)p;
	return v >= 0x00010000 && v <= 0xC0000000 && !(v & 3);
}

bool Trim_ValidRide(DWORD* Ride)
{
	return Trim_ValidPtr(Ride);
}

// ---------------------------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------------------------

// Name hash and parent per car type, built once. Both answers are wanted on paths that run per
// car per frame, and working them out from the strings every time turns every one of those into
// a scan of the whole car table.
std::vector<DWORD> TrimNameHashes;
std::vector<int> TrimParents;

// Whether a car has any parts carrying TRIM. Worked out lazily, see Trim_ScanVariants.
std::vector<int> TrimHasVariants; // -1 not looked at yet, 0 no, 1 yes

DWORD Trim_NameHashOf(int CarType)
{
	if (CarType < 0 || CarType >= (int)TrimNameHashes.size()) return 0;

	return TrimNameHashes[CarType];
}

int Trim_CarTypeFromNameHash(DWORD Hash)
{
	if (!Hash) return -1;

	for (int i = 0; i < (int)TrimNameHashes.size(); i++)
		if (TrimNameHashes[i] == Hash) return i;

	return -1;
}

// The car this one is a trim of, or -1 when it is a car in its own right.
int Trim_ParentOf(int CarType)
{
	if (CarType < 0 || CarType >= (int)TrimParents.size()) return -1;

	return TrimParents[CarType];
}

// Whether a trim can be picked yet. PresetCars.h owns the conditions and the checking, so a
// trim and a sponsor car are unlocked by exactly the same rules and the ini words are the same.
//
// This gates CHOOSING a trim, not keeping one. A car already wearing a trim that has since
// become locked, or that came out of a save made when it was not, keeps it and keeps working.
// Taking parts off a car the player already owns is not what an unlock means.
bool PresetUnlockSatisfied(int Condition, const char* Value); // PresetCars.h

bool Trim_Unlocked(int TrimType)
{
	if (TrimType < 0 || TrimType >= CarCount) return false;

	return PresetUnlockSatisfied(CarConfigs[TrimType].Main.UnlockCondition,
		CarConfigs[TrimType].Main.UnlockValue);
}

bool Trim_IsTrim(int CarType)
{
	return Trim_ParentOf(CarType) >= 0;
}

// Called once from LoaderCarInfo_Hook, right after LoadCarConfigs has read every TrimOf.
void Trim_BuildTables()
{
	TrimNameHashes.assign(CarCount, 0);
	TrimParents.assign(CarCount, -1);
	TrimHasVariants.assign(CarCount, -1);

	for (int i = 0; i < CarCount; i++)
	{
		char const* Name = GetCarTypeName(i);
		TrimNameHashes[i] = (Name && Name[0]) ? bStringHash((char*)Name) : 0;
	}

	for (int i = 0; i < CarCount; i++)
		TrimParents[i] = Trim_CarTypeFromNameHash(CarConfigs[i].Main.TrimOf);

	// The whole table, because every name in this feature is a CollectionName and nothing else:
	// the ini a car reads is "<CollectionName>.ini", TrimOf names a CollectionName, and the trim
	// marker part is "<PARENT CollectionName>_<TRIM CollectionName>". A CollectionName does NOT
	// have to match the CARS folder the geometry lives in, and when it does not, an ini named
	// after the folder is read by nobody and the trim silently never exists.
	TrimTraceLine("car types: %d\n", CarCount);

	for (int i = 0; i < CarCount; i++)
		TrimTraceLine("  %3d  %-24s usage %d%s\n", i, GetCarTypeName(i),
			(int)CarTypeInfo_UsageType(CarConfigs[i].CarTypeInfo),
			(TrimParents[i] >= 0) ? "  <- trim" : "");

	for (int i = 0; i < CarCount; i++)
	{
		// TrimOf was set and named a car that is not in the table. Almost always a typo, or the
		// parent being spelled as its folder rather than its CollectionName.
		if (TrimParents[i] < 0 && CarConfigs[i].Main.TrimOf)
			TrimTraceLine("%s: TrimOf names a car type that does not exist, hash 0x%08X\n",
				GetCarTypeName(i), (unsigned int)CarConfigs[i].Main.TrimOf);

		if (TrimParents[i] < 0) continue;

		static char const* const ConditionNames[] = { "None", "Code", "Event", "Stage" };

		int Condition = CarConfigs[i].Main.UnlockCondition;

		TrimTraceLine("trim %s of %s, usage type %d, unlock %s %s\n", GetCarTypeName(i),
			GetCarTypeName(TrimParents[i]), (int)CarTypeInfo_UsageType(CarConfigs[i].CarTypeInfo),
			(Condition >= 0 && Condition <= 3) ? ConditionNames[Condition] : "?",
			CarConfigs[i].Main.UnlockValue);

		// UnlockSponsorCarsWithoutCheats short circuits every condition inside
		// PresetUnlockSatisfied, which is right for a sponsor car and surprising here: with it on,
		// a trim gated on an event or a stage is simply always available. Worth one line rather
		// than a test round spent wondering why the gate does nothing.
		//
		// 0 rather than PRESET_UNLOCK_NONE because that lives in PresetCars.h, which is included
		// after this file. Only the value crosses over, not the name.
		if (Condition != 0 && UnlockSponsorCarsWithoutCheats)
			TrimTraceLine("  note: [SponsorCars] UnlockWithoutCheats = 1 opens this trim regardless\n");
	}
}

// Every trim of a car, in car type order. The parent itself is not included, so a car with one
// trim cycles between "no trim" and that trim.
int Trim_ListFor(int CarType, int* Out, int Max)
{
	if (CarType < 0 || CarType >= CarCount) return 0;

	int n = 0;

	for (int i = 0; i < CarCount && n < Max; i++)
		if (Trim_ParentOf(i) == CarType) Out[n++] = i;

	return n;
}

// ---------------------------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------------------------

// A part has three names and only one of them is its identity.
//
//   PartNameHash at +0x00     the real one. PresetCarSlot::FillWithRide packs exactly this
//                             into the save as (word[+2] << 16) | word[+0], and FillRideWith
//                             hands it back to NewGetCarPart to rebuild the part.
//   NameOffset at +0x08       a string table offset. This is what CarPart_GetName returns and
//                             it is the DEBUG name, the label Binary shows in its tree. It is
//                             for looking at, not for matching.
//   the attributes below      what this feature actually goes by.
//
// The first cut of this read trims out of the debug name, which worked only because the test
// data happened to have matching debug names.
// ---------------------------------------------------------------------------------------------

#define TRIM_ATTR_TRIM     CT_bStringHash("TRIM")
#define TRIM_ATTR_REPLACES CT_bStringHash("TRIM_REPLACES")

// Which version of the car this part belongs to, 0 when it belongs to all of them.
DWORD Trim_PartBelongsTo(DWORD* Part)
{
	if (!Part) return 0;

	return CarPart_GetAppliedAttributeUParam(Part, TRIM_ATTR_TRIM, 0);
}

DWORD Trim_PartReplaces(DWORD* Part)
{
	if (!Part) return 0;

	return CarPart_GetAppliedAttributeUParam(Part, TRIM_ATTR_REPLACES, 0);
}

DWORD Trim_PartNameHash(DWORD* Part)
{
	if (!Part) return 0;

	char const* Name = CarPart_GetName(Part);

	return (Name && Name[0]) ? bStringHash((char*)Name) : 0;
}

// Which of a part's names Binary writes into a Key attribute is still unverified, so
// TRIM_REPLACES is matched against both the real one and the debug one. One extra compare
// takes a whole class of "it silently found nothing" out of the first test.
bool Trim_PartIsNamed(DWORD* Part, DWORD NameHash)
{
	if (!Part || !NameHash) return false;

	if (*(DWORD*)Part == NameHash) return true; // PartNameHash

	return Trim_PartNameHash(Part) == NameHash; // bStringHash of the debug name
}

// ---------------------------------------------------------------------------------------------
// The marker part
//
// Which version a car is wearing is a part in RIGHT_SIDE_MIRROR carrying TRIM. Nothing else
// installs there: LEFT_SIDE_MIRROR and RIGHT_SIDE_MIRROR appear in the whole tree only inside
// the carbon slot list, and the mirrors a car actually wears live in WING_MIRROR as one part
// for both sides.
//
// There is a marker for the base car too, carrying TRIM = <the car's own name>. Worth having
// rather than leaving the slot empty, because RideInfo::SetStockParts fills every slot with
// its first part, so a fresh car would otherwise come out wearing whichever marker happens to
// be first in the database.
// ---------------------------------------------------------------------------------------------

// LEGACY. Before TRIM existed the marker was recognised by its debug name being <CAR>_<TRIM>.
// Still honoured so existing data keeps working, but the attribute wins and new parts should
// only use the attribute: a debug name is a label and nothing stops two parts sharing one.
DWORD Trim_MarkerNameFallback(int CarType, DWORD* Part)
{
	if (!Part) return 0;

	char const* Name = CarPart_GetName(Part);
	if (!Name || !Name[0]) return 0;

	char const* CarName = GetCarTypeName(CarType);
	if (!CarName || !CarName[0]) return 0;

	// <CAR>_<TRIM>, so skip the car name and the underscore
	size_t Prefix = strlen(CarName);

	if (strncmp(Name, CarName, Prefix) != 0 || Name[Prefix] != '_') return 0;

	return bStringHash((char*)(Name + Prefix + 1));
}

// The version name a marker part stands for, 0 if it is not a marker at all.
DWORD Trim_MarkerVersion(int CarType, DWORD* Part)
{
	DWORD Attribute = Trim_PartBelongsTo(Part);

	return Attribute ? Attribute : Trim_MarkerNameFallback(CarType, Part);
}

// The marker part for one version of this car. Walking the slot is the only way: NewGetCarPart
// matches on PartNameHash, which is not what is being looked for here.
DWORD* Trim_MarkerPartFor(int CarType, DWORD VersionHash)
{
	if (!VersionHash) return nullptr;

	for (DWORD* Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, CARSLOTID_RIGHT_SIDE_MIRROR, 0, 0, -1);
		Part;
		Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, CARSLOTID_RIGHT_SIDE_MIRROR, 0, Part, -1))
	{
		if (Trim_MarkerVersion(CarType, Part) == VersionHash) return Part;
	}

	return nullptr;
}

DWORD* Trim_PartFor(int CarType, int TrimType)
{
	if (TrimType < 0 || TrimType >= CarCount) return nullptr;

	return Trim_MarkerPartFor(CarType, Trim_NameHashOf(TrimType));
}

// Which trim this car is wearing, or -1 for none.
int Trim_OnRide(DWORD* Ride)
{
	if (!Trim_ValidRide(Ride)) return -1;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return -1;

	DWORD* Installed = (DWORD*)Ride[356 + CARSLOTID_RIGHT_SIDE_MIRROR];

	int Trim = Trim_CarTypeFromNameHash(Trim_MarkerVersion(CarType, Installed));

	// The marker for the base car names the car itself, which is not a trim of anything, so this
	// also turns that into "no trim" without a special case.
	if (Trim >= 0 && Trim_ParentOf(Trim) != CarType) return -1;

	return Trim;
}

// The version of the car a ride is wearing, as a name hash. Never 0: with no trim fitted the
// car is its own version, which is what lets a part be marked as belonging to the base car.
DWORD Trim_VersionOnRide(DWORD* Ride)
{
	if (!Trim_ValidRide(Ride)) return 0;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return 0;

	int Trim = Trim_OnRide(Ride);

	return Trim_NameHashOf(Trim >= 0 ? Trim : CarType);
}
// ---------------------------------------------------------------------------------------------
// Trim part variants
//
// A trim usually changes more than a badge. TURBOSUPRA wants its own hoods, its own headlights,
// its own bumpers, and it wants them to stand in for the stock ones rather than sit next to them
// in the list. HIDESLOT cannot express that: it removes a slot rather than replacing a part, and
// it only goes one way.
//
// Two Custom attributes on a car part, authored in Binary exactly the way SWAPSLOT is:
//
//   TRIM            Key   ValueKey = the trim this part belongs to, e.g. TURBOSUPRA
//   TRIM_REPLACES   Key   ValueKey = the part it stands in for, e.g. SUPRA_KIT01_HOOD
//
// and four rules:
//
//   1. A part carrying TRIM is listed in the Body Shop only while that trim is fitted.
//   2. A part carrying TRIM and TRIM_REPLACES takes the named part's place: the named part drops
//      out of the list, and wherever it is installed on the car the variant goes in instead.
//   3. Taking the trim off puts the originals back, because the variant carries the name of what
//      it replaced. That symmetry is the whole point.
//   4. TRIM without TRIM_REPLACES is a trim only extra: it appears while its trim is on and
//      stands in for nothing.
//
// The attributes go on the NEW parts only. No vanilla part is touched, which is what makes "give
// every hood a TURBOSUPRA version" a job of editing the new hoods rather than all of them.
//
// ASSUMPTION, marked because it has not been proven in game: Binary stores a Key attribute as
// bStringHash of the text typed into it, so TRIM_REPLACES = SUPRA_KIT01_HOOD compares equal to
// bStringHash(CarPart_GetName(that part)). Trim_ScanVariants prints every TRIM part it finds
// along with what its TRIM_REPLACES resolves to, so one run with TrimTrace = 1 settles it. If the
// hashes turn out not to match, the fix is in Trim_PartNameHash and nowhere else.
// ---------------------------------------------------------------------------------------------

// The part in this slot that stands in for Replaced while VersionHash is the car being worn.
DWORD* Trim_VariantFor(int CarType, int Slot, DWORD VersionHash, DWORD* Replaced)
{
	if (!VersionHash || !Replaced) return nullptr;

	for (DWORD* Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, Slot, 0, 0, -1);
		Part;
		Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, Slot, 0, Part, -1))
	{
		if (Trim_PartBelongsTo(Part) != VersionHash) continue;

		if (Trim_PartIsNamed(Replaced, Trim_PartReplaces(Part))) return Part;
	}

	return nullptr;
}

// What a version should be wearing in a slot when the part on the car belongs to a different
// version and does not name a replacement. Prefer a part that claims this version, fall back to
// one no version has claimed.
//
// This is what makes TRIM alone useful. Marking the stock bumper TRIM = GOLF and the R32 one
// TRIM = GOLF_R32 is enough to have them swap, no TRIM_REPLACES needed, as long as only one
// part per slot claims each version. TRIM_REPLACES is for when several do and the pairing has
// to be spelled out.
DWORD* Trim_FirstPartForVersion(int CarType, int Slot, DWORD VersionHash)
{
	DWORD* Unclaimed = nullptr;

	for (DWORD* Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, Slot, 0, 0, -1);
		Part;
		Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, Slot, 0, Part, -1))
	{
		DWORD Belongs = Trim_PartBelongsTo(Part);

		if (Belongs == VersionHash) return Part;

		if (!Belongs && !Unclaimed) Unclaimed = Part;
	}

	return Unclaimed;
}

DWORD* Trim_PartByName(int CarType, int Slot, DWORD NameHash)
{
	if (!NameHash) return nullptr;

	for (DWORD* Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, Slot, 0, 0, -1);
		Part;
		Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, Slot, 0, Part, -1))
	{
		if (Trim_PartIsNamed(Part, NameHash)) return Part;
	}

	return nullptr;
}

// Does this car have any trim variants at all, and what are they. Walking every slot of every
// part is not something to do per frame, so the answer is worked out once per car type, and only
// for cars that actually declare trims: a car nothing is a TrimOf cannot have variants that
// matter, so it is never scanned.

bool Trim_ScanVariants(int CarType)
{
	// Only cars something declares TrimOf on. Everything else cannot have a variant that
	// matters, and walking every slot of every part of every car type is not free.
	int Trims[32];
	if (!Trim_ListFor(CarType, Trims, 32)) return false;

	TrimTraceLine("scanning %s for trim part variants\n", GetCarTypeName(CarType));

	bool Found = false;

	for (int Slot = CARSLOTID_MODEL_FIRST; Slot <= CARSLOTID_MODEL_LAST; Slot++)
	{
		if (Slot == CARSLOTID_RIGHT_SIDE_MIRROR) continue; // the trim marker itself

		for (DWORD* Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, Slot, 0, 0, -1);
			Part;
			Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, Slot, 0, Part, -1))
		{
			DWORD Belongs = Trim_PartBelongsTo(Part);
			if (!Belongs) continue;

			Found = true;

			if (!TrimTrace) continue;

			DWORD Replaces = Trim_PartReplaces(Part);
			DWORD* Original = Trim_PartByName(CarType, Slot, Replaces);
			int Owner = Trim_CarTypeFromNameHash(Belongs);

			char const* Name = CarPart_GetName(Part);

			TrimTraceLine("  variant %s in slot %d %s: TRIM = 0x%08X (%s), TRIM_REPLACES = 0x%08X -> %s\n",
				Name ? Name : "?", Slot, GetCarSlotIDName(Slot),
				(unsigned int)Belongs, (Owner >= 0) ? GetCarTypeName(Owner) : "NOT A TRIM OF THIS CAR",
				(unsigned int)Replaces,
				Original ? CarPart_GetName(Original) : (Replaces ? "NOTHING, check the name" : "(extra part)"));
		}
	}

	return Found;
}

bool Trim_CarHasVariants(int CarType)
{
	if (CarType < 0 || CarType >= CarCount) return false;

	if ((int)TrimHasVariants.size() != CarCount) TrimHasVariants.assign(CarCount, -1);

	if (TrimHasVariants[CarType] < 0)
		TrimHasVariants[CarType] = Trim_ScanVariants(CarType) ? 1 : 0;

	return TrimHasVariants[CarType] != 0;
}

// Every part the car is wearing, put in step with the trim it is wearing. Runs at the end of
// RideInfo_UpdatePartsEnabled, next to PartLink_ApplyVisibility, so nothing has to remember to
// call it: any route that changes a part ends up here.
//
// The scan probes two attributes per installed part and walks a slot for each swap, which is fine
// once and not fine on every part scroll, so it is skipped while nothing has moved. Same shape as
// PartLinkCachedSignature.
DWORD TrimResolveSignature = 0;
bool TrimResolveCacheValid = false;

DWORD Trim_RideSignature(DWORD* Ride)
{
	DWORD Signature = (DWORD)(*(int*)Ride) + 1;

	for (int Slot = CARSLOTID_MODEL_FIRST; Slot <= CARSLOTID_MODEL_LAST; Slot++)
		Signature = Signature * 0x21 + (DWORD)Ride[356 + Slot];

	return Signature;
}

void Trim_ResolveParts(DWORD* Ride)
{
	if (!Trim_ValidRide(Ride)) return;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return;

	if (!Trim_CarHasVariants(CarType))
	{
		TrimResolveCacheValid = false;
		return;
	}

	DWORD Signature = Trim_RideSignature(Ride);
	if (TrimResolveCacheValid && Signature == TrimResolveSignature) return;

	DWORD VersionHash = Trim_VersionOnRide(Ride);

	for (int Slot = CARSLOTID_MODEL_FIRST; Slot <= CARSLOTID_MODEL_LAST; Slot++)
	{
		if (Slot == CARSLOTID_RIGHT_SIDE_MIRROR) continue;

		DWORD* Part = (DWORD*)Ride[356 + Slot];
		if (!Part) continue;

		DWORD Belongs = Trim_PartBelongsTo(Part);

		// A part belonging to a version the car is not wearing any more. Put back what it says it
		// replaced. This is the half HIDESLOT never had.
		if (Belongs && Belongs != VersionHash)
		{
			DWORD* Original = Trim_PartByName(CarType, Slot, Trim_PartReplaces(Part));

			// It named nothing, so fall back to whatever this version is allowed to wear here.
			// Without this a part marked for one version and naming no replacement sits on the car
			// for ever: it drops out of the list, and nothing takes it off.
			if (!Original) Original = Trim_FirstPartForVersion(CarType, Slot, VersionHash);

			if (Original)
			{
				Ride[356 + Slot] = (DWORD)Original;
				Part = Original;
			}
		}

		// And whatever is there now, if this version has a stand-in for it, that goes on instead.
		// Already wearing the right variant is a no-op: nothing replaces a variant.
		DWORD* Variant = Trim_VariantFor(CarType, Slot, VersionHash, Part);

		if (Variant) Ride[356 + Slot] = (DWORD)Variant;
	}

	// The signature has to be taken again, because the loop above is what changed it
	TrimResolveSignature = Trim_RideSignature(Ride);
	TrimResolveCacheValid = true;
}

// Used by the Body Shop list builder, next to PartLink_IsHiddenFromMenu. The car being browsed is
// the customize manager's, which is gTheRideInfo.
bool Trim_IsPartListable(DWORD* Part, int CarType, int Slot)
{
	if (!Part) return true;
	if (!Trim_CarHasVariants(CarType)) return true;

	DWORD VersionHash = Trim_VersionOnRide((DWORD*)gTheRideInfo);

	DWORD Belongs = Trim_PartBelongsTo(Part);

	// A part that names a version exists only while the car is wearing that version. Marking a
	// part with the BASE car's name is how a part is kept out of the trims, which is the other
	// half of what people want and costs one attribute on the few parts that need it.
	if (Belongs) return Belongs == VersionHash;

	// An unmarked part is shared by every version, unless this one has a stand-in for it.
	if (Trim_VariantFor(CarType, Slot, VersionHash, Part)) return false;

	return true;
}

// ---------------------------------------------------------------------------------------------
// Which CarTypeInfo the badges come from
//
// Every screen that draws a car's badges does the same three instructions:
//
//     mov eax, [this+58h]      the SelectableCar it is drawing
//     mov ecx, [eax+918h]      its CarTypeInfo
//     mov eax, [ecx+840h]      its car type, which is all GetCarTypeLogoHash is given
//
// A car type is not enough to find a trim, because two saved cars can be the same car type
// wearing different trims. Reading the trim off the car viewer's ride answers with whichever car
// is on screen, which is right for the selected car and wrong for every other one: changing the
// trim on one saved SUPRA made the next saved SUPRA's badge follow it, while its performance,
// which is per ride, stayed correct. That mismatch is what gave the cause away.
//
// So the screens are wrapped at their call sites and the trim of the car they are about to draw
// is put where GetCarTypeLogoHash can see it, for exactly the length of that call. Nothing is
// kept afterwards, which is the whole difference from the captured this pointer that crashed the
// build before last.
// ---------------------------------------------------------------------------------------------

DWORD* (__thiscall* SelectableCar_GetRide)(DWORD* SelectableCar) = (DWORD * (__thiscall*)(DWORD*))0x511E00;

bool TrimOverrideActive = false;  // a screen is asking on behalf of one particular car
int TrimOverrideTrim = -1;        // and that car wears this trim, -1 for none

void Trim_PushOverrideFromRide(DWORD* Ride)
{
	TrimOverrideActive = false;
	TrimOverrideTrim = -1;

	if (!Trim_ValidRide(Ride)) return;

	TrimOverrideTrim = Trim_OnRide(Ride);
	TrimOverrideActive = true;
}

// The screens do NOT agree on where the selected car sits, which is worth checking before
// wrapping one rather than after:
//
//   UICareerCarLot::RefreshHeader          SelectableCar at +0x58
//   UIQRCarSelect::RefreshHeader           SelectableCar at +0x58
//   sub_4BE440, the customize brand logos  SelectableCar at +0x58
//   UICareerCribCarSelect::RefreshHeader   SelectableCar at +0x54
//   sub_4E7F50, the opponent ride screen   a RideInfo* at +0x124, no SelectableCar at all
void Trim_PushOverrideFromSelectableCar(DWORD* Screen, int Offset)
{
	TrimOverrideActive = false;
	TrimOverrideTrim = -1;

	if (!Trim_ValidPtr(Screen)) return;

	DWORD* Selected = (DWORD*)Screen[Offset / 4];
	if (!Trim_ValidPtr(Selected)) return;

	Trim_PushOverrideFromRide(SelectableCar_GetRide(Selected));
}

void Trim_PopOverride()
{
	TrimOverrideActive = false;
	TrimOverrideTrim = -1;
}

// The car type whose CarTypeInfo the badges and the name art should be read from.
int Trim_EffectiveCarType(int CarType)
{
	if (CarType < 0 || CarType >= CarCount) return CarType;

	// A screen told us which car it is drawing, so that is the answer, trim or no trim. The
	// parent check keeps a trim from leaking onto a different car that happens to ask inside the
	// same call.
	if (TrimOverrideActive)
	{
		if (TrimOverrideTrim >= 0 && Trim_ParentOf(TrimOverrideTrim) == CarType) return TrimOverrideTrim;

		return CarType;
	}

	// Nobody said, so fall back to the car on screen. CarViewer_GetRideInfo(0) at 0x4A7890
	// returns TopOrFullScreenRide and follows it; gTheRideInfo does NOT, it belongs to the
	// customize manager and only moves when something is customized.
	DWORD* Ride = CarViewer_GetRideInfo(0);

	if (Trim_ValidRide(Ride) && *(int*)Ride == CarType)
	{
		int Trim = Trim_OnRide(Ride);
		if (Trim >= 0 && Trim < CarCount) return Trim;
	}

	return CarType;
}

DWORD* Trim_CarTypeInfoForRide(DWORD* Ride)
{
	if (!Trim_ValidRide(Ride)) return nullptr;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return nullptr;

	int Trim = Trim_OnRide(Ride);

	if (Trim >= 0 && Trim < CarCount) return CarConfigs[Trim].CarTypeInfo;

	return CarConfigs[CarType].CarTypeInfo;
}

DWORD* Trim_CarTypeInfoForCarType(int CarType)
{
	if (CarType < 0 || CarType >= CarCount) return nullptr;

	return CarConfigs[Trim_EffectiveCarType(CarType)].CarTypeInfo;
}

// ---------------------------------------------------------------------------------------------
// Wrapping the screens that draw badges
//
// All five are thiscall(Screen*) and all five read the selected car from [this+0x58], so one
// wrapper shape covers them. These are pass through: the screen pointer is used only while the
// game is inside the call, never stored for later.
// ---------------------------------------------------------------------------------------------

typedef void(__thiscall* RefreshHeaderFn)(DWORD* Screen);

RefreshHeaderFn GameCarLotRefreshHeader   = (RefreshHeaderFn)0x4AFE00; // UICareerCarLot
RefreshHeaderFn GameCribRefreshHeader     = (RefreshHeaderFn)0x4B0140; // UICareerCribCarSelect
RefreshHeaderFn GameQRSelectRefreshHeader = (RefreshHeaderFn)0x4B2310; // UIQRCarSelect
RefreshHeaderFn GameCustomizeBrands       = (RefreshHeaderFn)0x4BE440; // brand logos in customize
RefreshHeaderFn GameOtherCarSelect        = (RefreshHeaderFn)0x4E7F50;

#define TRIM_WRAP_SELECTABLE(name, target, offset)                \
	void __fastcall name(DWORD* Screen, void* EDX_Unused)         \
	{                                                             \
		Trim_PushOverrideFromSelectableCar(Screen, offset);       \
		target(Screen);                                           \
		Trim_PopOverride();                                       \
	}

TRIM_WRAP_SELECTABLE(TrimCarLotRefreshHeader,   GameCarLotRefreshHeader,   0x58)
TRIM_WRAP_SELECTABLE(TrimCribRefreshHeader,     GameCribRefreshHeader,     0x54)
TRIM_WRAP_SELECTABLE(TrimQRSelectRefreshHeader, GameQRSelectRefreshHeader, 0x58)
TRIM_WRAP_SELECTABLE(TrimCustomizeBrands,       GameCustomizeBrands,       0x58)

// The opponent ride screen holds a RideInfo outright, so there is no SelectableCar to ask.
void __fastcall TrimOtherCarSelect(DWORD* Screen, void* EDX_Unused)
{
	Trim_PushOverrideFromRide(Trim_ValidPtr(Screen) ? (DWORD*)Screen[0x124 / 4] : nullptr);
	GameOtherCarSelect(Screen);
	Trim_PopOverride();
}

// GarageMainScreen::SetCarType builds SECONDARY_LOGO_<name> straight off the CarTypeInfo instead
// of going through GetCarTypeLogoHash, so the garage name art is a second place to redirect. It
// indexes the array by car type, which means handing it the trim's car type is the whole fix.
//
// Its five call sites are hooked rather than its entry, because a hook on the entry that wants to
// call the original has nowhere left to call: the entry is the hook.
void(__thiscall* GameGarageMainScreenSetCarType)(DWORD* Screen, int CarType) = (void(__thiscall*)(DWORD*, int))0x4A5EE0;

void __fastcall GarageMainScreen_SetCarType(DWORD* Screen, void* EDX_Unused, int CarType)
{
	GameGarageMainScreenSetCarType(Screen, Trim_EffectiveCarType(CarType));
}

// ---------------------------------------------------------------------------------------------
// Applying a trim
//
// Writing RideInfo[356 + slot] does not update what is on screen. That is what made the first
// build look like it changed nothing but the badges: the part went in, the paint went in, and
// the car standing there was the one that had already been built.
//
// CarCustomizeManager::InstallPart at 0x55C230 is the game's own answer and does all of it:
//
//     RideInfo::SetPart(manager+0x940, slot, part)
//     ... slot specific cleanups, trunk audio, hood vinyls, carbon vinyls
//     RideInfo::operator=(manager+0x50, manager+0x940)
//     CarViewer::SetRideInfo(ride, 1, 0)                     rebuilds the car on screen
//     PresetCarSlot::FillWithRide([manager+0x2C]+0x18, ride) writes the record the save reads
//     StarGazerGuide::NotifyVisualRatingChange
//
// So a trim is installed exactly the way the Body Shop installs a bumper, and the refresh, the
// record and the rep rating all come along.
// ---------------------------------------------------------------------------------------------

#define CarCustomizeManager_RideInfo (0x940 / 4)

// Byte 0 of the manager is its "has control" flag: BeginCarCustomize sets it to 1 at 0x552DF2 and
// CarCustomizeManager::UnTakeControl clears it at 0x54FDAA. That is a real customize session, as
// opposed to the menu state, which reports 0x20 for the car lot as well.
//
// The build before this one compared the viewer ride against the manager ride part for part
// instead. It worked, but only once the parts screen had synced the two, so the key did nothing
// until you had been into Parts once. Eleven slots differ before that, WIDE_BODY first.
bool Trim_CustomizeSessionActive()
{
	return *(BYTE*)gCarCustomizeManager == 1;
}

DWORD* Trim_CustomizeRide()
{
	return (DWORD*)gCarCustomizeManager + CarCustomizeManager_RideInfo;
}

// Put a trim on the car the customize manager is editing. Everything visible follows from the
// two InstallPart calls; nothing is written into a RideInfo by hand.
bool Trim_Set(int CarType, int TrimType)
{
	DWORD* Manager = (DWORD*)gCarCustomizeManager;

	// Going back to no trim installs the base car's own marker when there is one, rather than
	// emptying the slot. RideInfo::SetStockParts fills every slot with its first part, so an
	// empty RIGHT_SIDE_MIRROR is not a state the game would ever produce on its own.
	DWORD* Part = Trim_MarkerPartFor(CarType, Trim_NameHashOf(TrimType >= 0 ? TrimType : CarType));

	if (TrimType >= 0 && !Part)
	{
		TrimTraceLine("%s: no RIGHT_SIDE_MIRROR part carries TRIM = %s\n",
			GetCarTypeName(CarType), GetCarTypeName(TrimType));
		return false;
	}

	CarCustomizeManager_InstallPart(Manager, CARSLOTID_RIGHT_SIDE_MIRROR, Part);

	// DefaultBasePaint, installed as a part the same way the Paint Shop does it, and it follows
	// the trim BOTH ways. Coming off one the car goes back to its own colour, which is what
	// makes a stock SUPRA look like a stock SUPRA again instead of keeping whatever the last
	// trim painted it.
	//
	// The cost of that is real and deliberate: switching trim overwrites a colour picked in the
	// Paint Shop. A trim owns its colour, so there is nowhere to put a player choice that the
	// next switch would not have to throw away anyway.
	{
		int PaintFrom = (TrimType >= 0) ? TrimType : CarType;

		DWORD Paint = CarTypeInfo_DefaultBasePaint(CarConfigs[PaintFrom].CarTypeInfo);

		DWORD* PaintPart = Paint
			? CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, CARSLOTID_BASE_PAINT, Paint, 0, -1)
			: nullptr;

		if (PaintPart) CarCustomizeManager_InstallPart(Manager, CARSLOTID_BASE_PAINT, PaintPart);
		else if (Paint) TrimTraceLine("  paint 0x%08X not available on %s\n",
			(unsigned int)Paint, GetCarTypeName(CarType));
	}

	// Physics comes out of the trim's CarTypeInfo the next time it is rebuilt. InstallPart never
	// asks for one, because a visual part does not change the numbers, so ask here.
	RideInfo_RebuildPhysicsInfo(Trim_CustomizeRide(), 0, true, true, true);

	TrimTraceLine("%s: trim -> %s\n", GetCarTypeName(CarType),
		TrimType >= 0 ? GetCarTypeName(TrimType) : "(none)");

	return true;
}

// The next trim in the cycle for this car, wrapping back through "no trim" at the end.
// Returns -2 when the car has no trims at all, which is not the same answer as -1.
int Trim_Next(int CarType, int Current)
{
	int Trims[32];
	int Count = Trim_ListFor(CarType, Trims, 32);

	if (!Count) return -2;

	int At = -1;

	for (int i = 0; i < Count; i++)
		if (Trims[i] == Current) { At = i; break; }

	// Walk forward and off the end into "no trim", stepping over anything still locked. The
	// filtering is here rather than in Trim_ListFor because that one also answers whether a car
	// is worth scanning for part variants, and that answer is cached for the run: it must not
	// depend on how far the career has got.
	for (int i = At + 1; i < Count; i++)
	{
		if (Trim_Unlocked(Trims[i])) return Trims[i];

		TrimTraceLine("  %s is still locked, skipped\n", GetCarTypeName(Trims[i]));
	}

	return -1;
}

// ---------------------------------------------------------------------------------------------
// Cycling
// ---------------------------------------------------------------------------------------------

bool TrimSwapKeyWasDown = false;

// Menu state, kept for the My Cars lock that still has to be written. The car lot reports the
// same 0x20 as customizing, so it cannot tell those two apart and is not used as a gate here.
#define _profileData     0x83A9D0
#define profileMenuState *(DWORD*)(_profileData + 0x156A8)

#define MENU_STATE_CAREER_MENU    0x01
#define MENU_STATE_MAIN_MENU      0x02
#define MENU_STATE_2P_SPLITSCREEN 0x04
#define MENU_STATE_CAR_CUSTOMIZE  0x20

void Trim_PollKey()
{
	if (!TrimSwapKey) return;

	bool Down = (GetAsyncKeyState(TrimSwapKey) & 0x8000) != 0;

	if (!Down) { TrimSwapKeyWasDown = false; return; }
	if (TrimSwapKeyWasDown) return;

	TrimSwapKeyWasDown = true;

	// The poll sits in CarRenderInfo::Render, which also runs in a race. The front end is a hard
	// precondition rather than something the menu state happens to imply.
	if (*(int*)_TheGameFlowManager != 3) return; // TheGameFlowManager->mCurrentState, 3 = front end

	// Customize only, for now. It is the one place the car can actually be rebuilt, and it is
	// where the trim browser is going to live. In the car lot the badges already follow a trim
	// that is on the car, there is just no way to change it there yet.
	if (!Trim_CustomizeSessionActive()) return;

	DWORD* Ride = Trim_CustomizeRide();
	if (!Trim_ValidRide(Ride)) return;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return;

	int Next = Trim_Next(CarType, Trim_OnRide(Ride));

	if (Next == -2)
	{
		TrimTraceLine("%s: no trims declare TrimOf = %s\n",
			GetCarTypeName(CarType), GetCarTypeName(CarType));
		return;
	}

	Trim_Set(CarType, Next);
}

void InitTrimSwap()
{
	// Call sites rather than the function entries, so no prologue has to be replayed and the
	// originals stay callable.

	// Every screen that draws a car's badges, so the trim comes from the car being drawn rather
	// than from whichever car the viewer happens to hold
	injector::MakeCALL(0x4B00EF, TrimCarLotRefreshHeader, true);   // UICareerCarLot
	injector::MakeCALL(0x4ED93C, TrimCarLotRefreshHeader, true);

	injector::MakeCALL(0x4B0664, TrimCribRefreshHeader, true);     // UICareerCribCarSelect

	injector::MakeCALL(0x4E28E3, TrimQRSelectRefreshHeader, true); // UIQRCarSelect
	injector::MakeCALL(0x4EEFA4, TrimQRSelectRefreshHeader, true);
	injector::MakeCALL(0x4FA41E, TrimQRSelectRefreshHeader, true);
	injector::MakeCALL(0x4FC576, TrimQRSelectRefreshHeader, true);

	injector::MakeCALL(0x4E889A, TrimCustomizeBrands, true);       // sub_4BE440, from sub_4E8530
	injector::MakeCALL(0x4F26C0, TrimOtherCarSelect, true);        // sub_4E7F50

	// Garage name art, the one SECONDARY_LOGO that does not go through GetCarTypeLogoHash
	injector::MakeCALL(0x4B00DC, GarageMainScreen_SetCarType, true); // GarageMainScreen::SetCarType
	injector::MakeCALL(0x4B065D, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4D91D1, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4E27C9, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4E8780, GarageMainScreen_SetCarType, true);
}
