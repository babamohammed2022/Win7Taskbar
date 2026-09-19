// Win7Taskbar - texture di hover del pannello overflow nativo
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v3.8 (1.21.49): LA STESSA texture di vetro che la system tray accende
// sotto le icone al passaggio del mouse (chiave XAML TrayTileHoverImage).
// Il base64 e' estratto byte-per-byte da
// src/Win7Taskbar/Utilities/GraphicalResourceBundle.cs
// (voce "win7_tray_tilehover", PNG 31x27): DO NOT EDIT BY HAND,
// rigeneralo dalla voce del bundle se la texture cambia.
//
// Decodifica una volta sola (DecodeEmbeddedPng -> MakeHBitmapFromArgb):
// un base64 corrotto ritorna false e il pannello ripiega sul rettangolo
// classico, mai un crash (punto 14, come gli altri asset incorporati).

#pragma once

namespace w7t {
namespace overflowtile {

/* win7_tray_tilehover.png (31x27) <- GraphicalResourceBundle.cs */
static const char kTrayTileHover[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAB8AAAAbCAYAAACEP1QvAAAACXBIWXMAAA7EAAAOxAGVKw4bAAACm0lEQVRIicXWv3LbRhDH8e/eHQCGpGRzwkSKZ5wu
qVLmXfKOeRaXqqwZF/ZMosiWqX8miTvc/lIIyozHYuIK2QrFAh8sbm9xAJ2kGRPG6HUJiECYEgcS4GG8mLTy0UsJaCR9M6U8ekpAl3OeT4mPXk1AZ2aT
4qOXA9DVWifFR697xCdtuNF7wN19Unz0ugC07t5NjHdAG4DWzNKU+Oi1AYg550kn3OjFwMN4/T8iBkC11jqlOnoKQHX3MiU+ejUAxd3zxHgGSgAKsJ8S
H72SgP3ES06tdQ/sE7BPKfnX3ijJgHgu/bNLfjKrQDUzfc0zUkpbICdgl3P+z9IlxXfv1L7Z0JZE43saM0xC5zMNcbD89q3nly8t28PLHIyc8+4R7yX9
a/KZ1J5fadZ0Nh8y8+LqEG2AIJlyVm6r+n7G9s2G7Zm0+8XsYBNL2gHDIz4cSnzt3nU3zO+CHZWiowJLCgsXnRsxSK5C74mtxL3Q7fEN8bX7p59D6A/g
e8bDRF9KsaeSXklN3DC7lZYqPB/EanCeuXgmmOEkC7gZe1VuY+BaxeJdFUtMryT/1eyLGVJK2TEeo/pa6xe4pHBxofY6sQgDx71YDZX14Kyr20po4SJZ
xaOxHSKbJJqKrA14P6jOLq1Iqmb2WUPXWntAT1YMD+vMjZbqWVVnXZzTMnCaKycu1o4d42oIDGbcR+MqGpdd5M8mcNE1vA8z+xiWujv0+Q/+Sld/kDbJ
2sE188oyDxwPslV1fVfF6SA9F3ShkiPcEYkYQwlsA9xXcW+92qNPJEn5qW34WeW/n6mdOws6Vilp3Yg1cCLxwuGFix9cnBh8v2j5tgnMerd832tjcBMC
VwHem/GX4AJx6caHwfkYnOsgPtwuuf7tx7AD+BuM36qzzy+RlgAAAABJRU5ErkJggg==
)W7B";

} /* namespace overflowtile */
} /* namespace w7t */
