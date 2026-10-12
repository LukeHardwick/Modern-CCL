# T21 — Heightmap terrain per database (rendering + HAT/HOT)

Tools track · Depends on T20 · Runs beside T22 and T24 · Branch `task/t21-heightmap-terrain`
Follow the shared rules in [README.md](README.md) (rule 6 does not apply).

## Goal
A database can use real elevation data instead of the synthetic hills. The **same** terrain
answers HAT/HOT (headless, so `demo_ig` and `ig_viewer` agree) and is drawn by the viewer.

## Design
- `TerrainSource` interface in `cigi_mock_ig`: `height(lat, lon)`, `normal(lat, lon)`,
  `material(lat, lon)`, `bounds()`. Implementations: `SyntheticTerrain` (today's formula, moved
  behind the interface) and `HeightmapTerrain`. `HatHotResponder` and `SceneModel` (ground clamp)
  use the database's source; outside the bounds HAT/HOT stays invalid (ICD 4.1.24, 4.2.2).
- Formats (no new dependencies; GeoTIFF needs GDAL/libtiff, so it is out of scope; document the
  `gdal_translate` conversions instead):
  - SRTM `.hgt` (1 and 3 arc-second, big-endian int16, void = -32768, filled from neighbours);
    the tile name gives its south-west corner. Several tiles per database are allowed.
  - Raw grid: little-endian int16 or float32 plus bounds, width, height, scale and offset in the
    manifest.
  - Optional material raster (uint8, same grid) or height bands taken from `[[material]]`.
- Bilinear height, normal from central differences in metres (reuse the geo helpers), heights
  above MSL.
- Manifest (`[terrain] kind = "heightmap"`, `tiles = [...]` or `raw = {...}`), validated by
  `cigi_db validate`.
- Viewer: chunks built from `TerrainSource` instead of the formula. Simple distance-based LOD
  (two or three grid resolutions) so a 1 x 1 degree SRTM3 tile stays at 60 fps in Release.
  Colours come from the material table. The flat-earth ENU frame remains: document the error at the
  edges of a 1-degree tile.
- Sample data: the user supplies one SRTM3 tile (public domain, NASA/USGS; ~2.9 MB) at
  `databases/sample/terrain/` before launch. **Do not download anything.** Tests use small
  generated tiles written by the test itself.

## Acceptance
README rules 7–9; coverage ≥ 95 % on headless code. Tests: hand-computed heights at grid points,
cell centres and tile seams; void filling; byte order; bounds/invalid; HAT/HOT against a known
plane and a known ramp; synthetic results unchanged. Benchmark line in `docs/guide/ig-viewer.md`.
Screenshot of the sample database in the report.
