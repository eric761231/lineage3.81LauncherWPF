// OverlayAssets.h: loads an external pak (background/icon PNGs + strings.xml)
// so an overlay's appearance/text can be swapped without rebuilding the DLL.
// Generic pak-set loader/parser shared by any number of independent overlays
// (DisconnectOverlay, MimirPowerOverlay, ...) -- each calls OverlayAssets_Load
// with its own folder/pak name and gets back an opaque handle; state for
// different pak sets never overlaps.
#pragma once
#include <windows.h>
#include <gdiplus.h>

struct OverlayAssetSet; // opaque

// Reads Core-sibling <game root>\<folderName>\<pakBaseName>.pak/.idx (same
// fixed XOR key as LauncherDll's GetFileBuffer / TW13081901.pak) and parses
// strings.xml. Safe to call once per (folderName, pakBaseName); returns
// nullptr (not an error) if the pak isn't deployed, in which case callers
// should fall back to a hardcoded appearance.
OverlayAssetSet *OverlayAssets_Load(const char *folderName,
                                    const char *pakBaseName);

bool OverlayAssets_IsLoaded(OverlayAssetSet *set);

// Raw bytes of any packed entry (e.g. a caller-defined XML config that isn't
// the disconnect-specific "strings.xml" schema this file already auto-parses).
// Returned pointer is owned by `set` (valid as long as the OverlayAssetSet
// is, i.e. for the process lifetime); do not free it. Returns false if the
// entry isn't in the idx.
bool OverlayAssets_GetRawBytes(OverlayAssetSet *set, const char *name,
                               const BYTE **outData, size_t *outLen);

// Returns a GDI+ bitmap decoded from the pak entry `name` (e.g.
// "bg_disconnect.png"), or nullptr if not found/decoded. Caller does not own
// the returned pointer (cached internally); do not delete it.
Gdiplus::Bitmap *OverlayAssets_GetBitmap(OverlayAssetSet *set,
                                         const char *name);

// screen: e.g. "disconnect". key: "title" or "body_<reason>" / "body_default".
// Returns true and fills outBuf if found.
bool OverlayAssets_GetText(OverlayAssetSet *set, const char *screen,
                           const char *key, wchar_t *outBuf, size_t outChars);

// Button hit-rect override for `screen`, from strings.xml's
// btnX/btnY/btnW/btnH attributes. Returns false if not specified (caller
// should fall back to its own computed position).
bool OverlayAssets_GetButtonRect(OverlayAssetSet *set, const char *screen,
                                 RECT *outRc);

// Display size override for `screen`, from strings.xml's Screen
// width/height attributes. Background image is stretched to this size (can
// differ from the source PNG's native pixel dimensions). Returns false if
// not specified (caller should fall back to the bitmap's native size).
bool OverlayAssets_GetSize(OverlayAssetSet *set, const char *screen,
                           int *outW, int *outH);

// Reference game client resolution (strings.xml's Screen refW/refH) the
// width/height/btnX/Y/W/H values were designed at. Caller scales everything
// by (actual game client size / this) so the overlay tracks the game
// window's size instead of staying a fixed pixel size. Returns false if not
// specified (caller should skip scaling, i.e. use scale 1:1).
bool OverlayAssets_GetRefSize(OverlayAssetSet *set, const char *screen,
                              int *outW, int *outH);

// which: "title" or "body". Text placement rect override from strings.xml's
// titleRect/bodyRect="x,y,w,h" attributes, so text can be lined up pixel-
// perfect against a background image. Returns false if not specified.
bool OverlayAssets_GetTextRect(OverlayAssetSet *set, const char *screen,
                               const char *which, RECT *outRc);

// which: "title" or "body". Font point size override from strings.xml's
// titleFontSize/bodyFontSize. Returns false if not specified.
bool OverlayAssets_GetFontSize(OverlayAssetSet *set, const char *screen,
                               const char *which, int *outSize);

// which: "title" or "body". Font family override from strings.xml's
// titleFontFamily/bodyFontFamily (e.g. "標楷體"). Returns false if not
// specified (caller should fall back to its own default).
bool OverlayAssets_GetFontFamily(OverlayAssetSet *set, const char *screen,
                                 const char *which, wchar_t *outBuf,
                                 size_t outChars);

// strings.xml's Screen showTitle="false" -> caller should skip drawing the
// title line entirely. Defaults to true if not specified.
bool OverlayAssets_GetShowTitle(OverlayAssetSet *set, const char *screen);

// which: "title" or "body". strings.xml's titleAlign/bodyAlign="center" ->
// true (center both axes); default/"left" or unspecified -> false.
bool OverlayAssets_GetCenterAlign(OverlayAssetSet *set, const char *screen,
                                  const char *which);

// Explicit top-left position override for `screen`, from strings.xml's
// Screen posX/posY attributes (offset from the game window's client-area
// top-left corner, in the same refW/refH coordinate space as everything
// else -- caller scales by its own computed scale factors). Returns false
// if not specified (caller should fall back to auto-centering).
bool OverlayAssets_GetPosition(OverlayAssetSet *set, const char *screen,
                               int *outX, int *outY);
