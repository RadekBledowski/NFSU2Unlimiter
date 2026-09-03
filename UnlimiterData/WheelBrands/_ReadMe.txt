Optional. UnlimiterData\_RimBrands.ini is the brand list; anything here is applied on top of it.

One brand per .ini file. A file naming a brand the ini already has replaces that entry and keeps
its place in the list; a file naming a new one is appended. Only the first section in the file is
read, whatever it is called, so a block copied straight out of _RimBrands.ini works unchanged:

    [RimBrand]
    BrandName = "MYBRAND"
    String = "RIMS_BRAND_MYBRAND"
    Texture = "DECAL_ICON_MYBRAND"
    AvailableForRegularCars = 1
    AvailableForSUVs = 0

Order among appended brands is filename order. Two reserved names: _Custom.ini replaces the
car-specific brand at index 0, and _Settings.ini can override RemoveRimSizeRestrictions.

BrandName is matched against the BRAND_NAME attribute on the rim parts, so it has to be exactly
what the parts say. Note that brand lists differ between installs: UG2NET calls one brand
STREETSPIN where vanilla calls it DAVIN.
