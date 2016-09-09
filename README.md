OpenFX-Arena [![Build Status](https://travis-ci.org/olear/openfx-arena.svg)](https://travis-ci.org/olear/openfx-arena)
============

A set of [OpenFX](http://openfx.sf.net) plugins designed for [Natron](http://natron.fr), but also compatible with other hosts.

Extra.ofx
=========

Essential plugins for Natron.

 * OpenRaster
 * ReadCDR
 * ReadKrita
 * ReadSVG
 * ReadPDF
 * Text

[Read more](Extra/README.md)

Magick.ofx
==========

Various plugins using ImageMagick.

 * Arc
 * Charcoal
 * Edges
 * Implode
 * Modulate
 * Oilpaint
 * Polar
 * Polaroid
 * ReadMisc
 * ReadPSD(XCF)
 * Reflection
 * Roll
 * Sketch
 * Swirl
 * Text (Deprecated)
 * Texture
 * Tile
 * Wave

[Read more](Magick/README.md)

OCL.ofx
=======

Various plugins using OpenCL.

[Read more](OCL/README.md)

Build
=====

```
git clone https://github.com/olear/openfx-arena
cd openfx-arena
git submodule update -i --recursive
make CONFIG=release
sudo make CONFIG=release install
```

OCL is currently optional, and can be built using:
```
cd OCL
make CONFIG=release
sudo make CONFIG=release install
```

License
=======

openfx-arena is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License version 2 as published by the Free Software Foundation.
