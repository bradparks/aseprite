/* Aseprite
 * Copyright (C) 2001-2013  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ico.c - Based on the code of Elias Pschernig.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/document.h"
#include "app/file/file.h"
#include "app/file/file_format.h"
#include "app/file/file_handle.h"
#include "app/file/format_options.h"
#include "base/cfile.h"
#include "raster/raster.h"

#include <allegro/color.h>

namespace app {

using namespace base;
  
class IcoFormat : public FileFormat {
  const char* onGetName() const { return "ico"; }
  const char* onGetExtensions() const { return "ico"; }
  int onGetFlags() const {
    return
      FILE_SUPPORT_LOAD |
      FILE_SUPPORT_SAVE |
      FILE_SUPPORT_RGB |
      FILE_SUPPORT_GRAY |
      FILE_SUPPORT_INDEXED;
  }

  bool onLoad(FileOp* fop);
  bool onSave(FileOp* fop);
};

FileFormat* CreateIcoFormat()
{
  return new IcoFormat;
}

struct ICONDIR {
  uint16_t reserved;
  uint16_t type;
  uint16_t entries;
};

struct ICONDIRENTRY {
  uint8_t  width;
  uint8_t  height;
  uint8_t  color_count;
  uint8_t  reserved;
  uint16_t planes;
  uint16_t bpp;
  uint32_t image_size;
  uint32_t image_offset;
};

struct BITMAPINFOHEADER {
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint16_t planes;
  uint16_t bpp;
  uint32_t compression;
  uint32_t imageSize;
  uint32_t xPelsPerMeter;
  uint32_t yPelsPerMeter;
  uint32_t clrUsed;
  uint32_t clrImportant;
};

bool IcoFormat::onLoad(FileOp* fop)
{
  FileHandle f(fop->filename.c_str(), "rb");

  // Read the icon header
  ICONDIR header;
  header.reserved = fgetw(f);                   // Reserved
  header.type     = fgetw(f);                   // Resource type: 1=ICON
  header.entries  = fgetw(f);                   // Number of icons

  if (header.type != 1) {
    fop_error(fop, "Invalid ICO file type.\n");
    return false;
  }

  if (header.entries < 1) {
    fop_error(fop, "This ICO files does not contain images.\n");
    return false;
  }

  // Read all entries
  std::vector<ICONDIRENTRY> entries;
  entries.reserve(header.entries);
  for (uint16_t n=0; n<header.entries; ++n) {
    ICONDIRENTRY entry;
    entry.width          = fgetc(f);     // width
    entry.height         = fgetc(f);     // height
    entry.color_count    = fgetc(f);     // color count
    entry.reserved       = fgetc(f);     // reserved
    entry.planes         = fgetw(f);     // color planes
    entry.bpp            = fgetw(f);     // bits per pixel
    entry.image_size     = fgetl(f);     // size in bytes of image data
    entry.image_offset   = fgetl(f);     // file offset to image data
    entries.push_back(entry);
  }

  // Read the first entry
  const ICONDIRENTRY& entry = entries[0];
  int width = (entry.width == 0 ? 256: entry.width);
  int height = (entry.height == 0 ? 256: entry.height);
  int numcolors = (entry.color_count == 0 ? 256: entry.color_count);
  PixelFormat pixelFormat = IMAGE_INDEXED;
  if (entry.bpp > 8)
    pixelFormat = IMAGE_RGB;

  // Create the sprite with one background layer
  Sprite* sprite = new Sprite(pixelFormat, width, height, numcolors);
  LayerImage* layer = new LayerImage(sprite);
  sprite->getFolder()->addLayer(layer);

  // Create the first image/cel
  Image* image = Image::create(pixelFormat, width, height);
  int image_index = sprite->getStock()->addImage(image);
  Cel* cel = new Cel(FrameNumber(0), image_index);
  layer->addCel(cel);
  image_clear(image, 0);

  // Go to the entry start in the file
  fseek(f, entry.image_offset, SEEK_SET);

  // Read BITMAPINFOHEADER
  BITMAPINFOHEADER bmpHeader;
  bmpHeader.size                 = fgetl(f);
  bmpHeader.width                = fgetl(f);
  bmpHeader.height               = fgetl(f); // XOR height + AND height
  bmpHeader.planes               = fgetw(f);
  bmpHeader.bpp                  = fgetw(f);
  bmpHeader.compression          = fgetl(f); // unused in .ico files
  bmpHeader.imageSize            = fgetl(f);
  bmpHeader.xPelsPerMeter        = fgetl(f); // unused for ico
  bmpHeader.yPelsPerMeter        = fgetl(f); // unused for ico
  bmpHeader.clrUsed              = fgetl(f); // unused for ico
  bmpHeader.clrImportant         = fgetl(f); // unused for ico

  // Read the palette
  if (entry.bpp <= 8) {
    Palette* pal = new Palette(FrameNumber(0), numcolors);

    for (int i=0; i<numcolors; ++i) {
      int b = fgetc(f);
      int g = fgetc(f);
      int r = fgetc(f);
      fgetc(f);

      pal->setEntry(i, _rgba(r, g, b, 255));
    }

    sprite->setPalette(pal, true);
    delete pal;
  }

  // Read XOR MASK
  int x, y, c, r, g, b;
  for (y=image->h-1; y>=0; --y) {
    for (x=0; x<image->w; ++x) {
      switch (entry.bpp) {

        case 8:
          c = fgetc(f);
          ASSERT(c >= 0 && c < numcolors);
          if (c >= 0 && c < numcolors)
            image_putpixel(image, x, y, c);
          else
            image_putpixel(image, x, y, 0);
          break;

        case 24:
          b = fgetc(f);
          g = fgetc(f);
          r = fgetc(f);
          image_putpixel(image, x, y, _rgba(r, g, b, 255));
          break;
      }
    }

    // every scanline must be 32-bit aligned
    while (x & 3) {
      fgetc(f);
      x++;
    }
  }

  // AND mask
  int m, v;
  for (y=image->h-1; y>=0; --y) {
    for (x=0; x<(image->w+7)/8; ++x) {
      m = fgetc(f);
      v = 128;
      for (b=0; b<8; b++) {
        if ((m & v) == v)
          image_putpixel(image, x*8+b, y, 0); // TODO mask color
        v >>= 1;
      }
    }

    // every scanline must be 32-bit aligned
    while (x & 3) {
      fgetc(f);
      x++;
    }
  }

  fop->document = new Document(sprite);
  return true;
}

bool IcoFormat::onSave(FileOp* fop)
{
  Sprite* sprite = fop->document->getSprite();
  int bpp, bw, bitsw;
  int size, offset, i;
  int c, x, y, b, m, v;
  FrameNumber n, num = sprite->getTotalFrames();

  FileHandle f(fop->filename.c_str(), "wb");

  offset = 6 + num*16;  // ICONDIR + ICONDIRENTRYs

  // Icon directory
  fputw(0, f);                  // reserved
  fputw(1, f);                  // resource type: 1=ICON
  fputw(num, f);                // number of icons

  // Entries
  for (n=FrameNumber(0); n<num; ++n) {
    bpp = (sprite->getPixelFormat() == IMAGE_INDEXED) ? 8 : 24;
    bw = (((sprite->getWidth() * bpp / 8) + 3) / 4) * 4;
    bitsw = ((((sprite->getWidth() + 7) / 8) + 3) / 4) * 4;
    size = sprite->getHeight() * (bw + bitsw) + 40;

    if (bpp == 8)
      size += 256 * 4;

    // ICONDIRENTRY
    fputc(sprite->getWidth(), f);       // width
    fputc(sprite->getHeight(), f);      // height
    fputc(0, f);                // color count
    fputc(0, f);                // reserved
    fputw(1, f);                // color planes
    fputw(bpp, f);              // bits per pixel
    fputl(size, f);             // size in bytes of image data
    fputl(offset, f);           // file offset to image data

    offset += size;
  }

  base::UniquePtr<Image> image(Image::create(sprite->getPixelFormat(),
                                       sprite->getWidth(),
                                       sprite->getHeight()));

  for (n=FrameNumber(0); n<num; ++n) {
    image_clear(image, 0);
    layer_render(sprite->getFolder(), image, 0, 0, n);

    bpp = (sprite->getPixelFormat() == IMAGE_INDEXED) ? 8 : 24;
    bw = (((image->w * bpp / 8) + 3) / 4) * 4;
    bitsw = ((((image->w + 7) / 8) + 3) / 4) * 4;
    size = image->h * (bw + bitsw) + 40;

    if (bpp == 8)
      size += 256 * 4;

    // BITMAPINFOHEADER
    fputl(40, f);                  // size
    fputl(image->w, f);            // width
    fputl(image->h * 2, f);        // XOR height + AND height
    fputw(1, f);                   // planes
    fputw(bpp, f);                 // bitcount
    fputl(0, f);                   // unused for ico
    fputl(size, f);                // size
    fputl(0, f);                   // unused for ico
    fputl(0, f);                   // unused for ico
    fputl(0, f);                   // unused for ico
    fputl(0, f);                   // unused for ico

    // PALETTE
    if (bpp == 8) {
      Palette *pal = sprite->getPalette(n);

      fputl(0, f);  // color 0 is black, so the XOR mask works

      for (i=1; i<256; i++) {
        fputc(_rgba_getb(pal->getEntry(i)), f);
        fputc(_rgba_getg(pal->getEntry(i)), f);
        fputc(_rgba_getr(pal->getEntry(i)), f);
        fputc(0, f);
      }
    }

    // XOR MASK
    for (y=image->h-1; y>=0; --y) {
      for (x=0; x<image->w; ++x) {
        switch (image->getPixelFormat()) {

          case IMAGE_RGB:
            c = image_getpixel(image, x, y);
            fputc(_rgba_getb(c), f);
            fputc(_rgba_getg(c), f);
            fputc(_rgba_getr(c), f);
            break;

          case IMAGE_GRAYSCALE:
            c = image_getpixel(image, x, y);
            fputc(_graya_getv(c), f);
            fputc(_graya_getv(c), f);
            fputc(_graya_getv(c), f);
            break;

          case IMAGE_INDEXED:
            c = image_getpixel(image, x, y);
            fputc(c, f);
            break;
        }
      }

      // every scanline must be 32-bit aligned
      while (x & 3) {
        fputc(0, f);
        x++;
      }
    }

    // AND MASK
    for (y=image->h-1; y>=0; --y) {
      for (x=0; x<(image->w+7)/8; ++x) {
        m = 0;
        v = 128;

        for (b=0; b<8; b++) {
          c = image_getpixel(image, x*8+b, y);

          switch (image->getPixelFormat()) {

            case IMAGE_RGB:
              if (_rgba_geta(c) == 0)
                m |= v;
              break;

            case IMAGE_GRAYSCALE:
              if (_graya_geta(c) == 0)
                m |= v;
              break;

            case IMAGE_INDEXED:
              if (c == 0) // TODO configurable background color (or nothing as background)
                m |= v;
              break;
          }

          v >>= 1;
        }

        fputc(m, f);
      }

        // every scanline must be 32-bit aligned
      while (x & 3) {
        fputc(0, f);
        x++;
      }
    }
  }

  return true;
}

} // namespace app
