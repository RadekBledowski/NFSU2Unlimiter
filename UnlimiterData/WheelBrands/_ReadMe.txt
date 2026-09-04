For additional rim brands only.

UnlimiterData\_RimBrands.ini is the brand list. Whatever it declares is what the game has, so an
install that renames or drops brands, as UG2NET does, works on its own with nothing in here.

This folder adds to that list. One brand per .ini file:

    [RimBrand]
    BrandName = "MYBRAND"
    String = "RIMS_BRAND_MYBRAND"
    Texture = "DECAL_ICON_MYBRAND"
    Car = 1
    Suv = 0

A file naming a brand _RimBrands.ini already declares replaces that entry instead of adding a
second one. Only the first section is read, whatever it is called, so a block copied straight out
of _RimBrands.ini works unchanged. Car and Suv can also be written as AvailableForRegularCars and
AvailableForSUVs; leaving one out means no.

The finished list is sorted alphabetically by BrandName, with the car-specific brand kept first,
so a brand added here lands on its own letter rather than at the end.

Two reserved names: _Custom.ini replaces the car-specific brand, _Settings.ini can override
RemoveRimSizeRestrictions.

Do not put copies of the brands _RimBrands.ini already has in here unless you mean to change them.
A file for a brand your install does not have adds it, and a brand with no rims and no label shows
up as an entry with no name and no icon.
