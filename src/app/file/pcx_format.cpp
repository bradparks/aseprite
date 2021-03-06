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
 * pcx.c - Based on the code of Shawn Hargreaves.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/file/file.h"
#include "app/file/file_format.h"
#include "app/file/file_handle.h"
#include "app/file/format_options.h"
#include "base/cfile.h"
#include "raster/raster.h"

#include <allegro/color.h>

namespace app {

using namespace base;

class PcxFormat : public FileFormat {
  const char* onGetName() const { return "pcx"; }
  const char* onGetExtensions() const { return "pcx"; }
  int onGetFlags() const {
    return
      FILE_SUPPORT_LOAD |
      FILE_SUPPORT_SAVE |
      FILE_SUPPORT_RGB |
      FILE_SUPPORT_GRAY |
      FILE_SUPPORT_INDEXED |
      FILE_SUPPORT_SEQUENCES;
  }

  bool onLoad(FileOp* fop);
  bool onSave(FileOp* fop);
};

FileFormat* CreatePcxFormat()
{
  return new PcxFormat;
}

bool PcxFormat::onLoad(FileOp* fop)
{
  int c, r, g, b;
  int width, height;
  int bpp, bytes_per_line;
  int xx, po;
  int x, y;
  char ch = 0;

  FileHandle f(fop->filename.c_str(), "rb");

  fgetc(f);                    /* skip manufacturer ID */
  fgetc(f);                    /* skip version flag */
  fgetc(f);                    /* skip encoding flag */

  if (fgetc(f) != 8) {         /* we like 8 bit color planes */
    fop_error(fop, "This PCX doesn't have 8 bit color planes.\n");
    return false;
  }

  width = -(fgetw(f));          /* xmin */
  height = -(fgetw(f));         /* ymin */
  width += fgetw(f) + 1;        /* xmax */
  height += fgetw(f) + 1;       /* ymax */

  fgetl(f);                     /* skip DPI values */

  for (c=0; c<16; c++) {        /* read the 16 color palette */
    r = fgetc(f);
    g = fgetc(f);
    b = fgetc(f);
    fop_sequence_set_color(fop, c, r, g, b);
  }

  fgetc(f);

  bpp = fgetc(f) * 8;          /* how many color planes? */
  if ((bpp != 8) && (bpp != 24)) {
    return false;
  }

  bytes_per_line = fgetw(f);

  for (c=0; c<60; c++)             /* skip some more junk */
    fgetc(f);

  Image* image = fop_sequence_image(fop, bpp == 8 ?
                                         IMAGE_INDEXED:
                                         IMAGE_RGB,
                                    width, height);
  if (!image) {
    return false;
  }

  if (bpp == 24)
    image_clear(image, _rgba(0, 0, 0, 255));

  for (y=0; y<height; y++) {       /* read RLE encoded PCX data */
    x = xx = 0;
    po = _rgba_r_shift;

    while (x < bytes_per_line*bpp/8) {
      ch = fgetc(f);
      if ((ch & 0xC0) == 0xC0) {
        c = (ch & 0x3F);
        ch = fgetc(f);
      }
      else
        c = 1;

      if (bpp == 8) {
        while (c--) {
          if (x < image->w)
            *(((uint8_t**)image->line)[y]+x) = ch;
          x++;
        }
      }
      else {
        while (c--) {
          if (xx < image->w)
            *(((uint32_t**)image->line)[y]+xx) |= (ch & 0xff) << po;
          x++;
          if (x == bytes_per_line) {
            xx = 0;
            po = _rgba_g_shift;
          }
          else if (x == bytes_per_line*2) {
            xx = 0;
            po = _rgba_b_shift;
          }
          else
            xx++;
        }
      }
    }

    fop_progress(fop, (float)(y+1) / (float)(height));
    if (fop_is_stop(fop))
      break;
  }

  if (!fop_is_stop(fop)) {
    if (bpp == 8) {                  /* look for a 256 color palette */
      while ((c = fgetc(f)) != EOF) {
        if (c == 12) {
          for (c=0; c<256; c++) {
            r = fgetc(f);
            g = fgetc(f);
            b = fgetc(f);
            fop_sequence_set_color(fop, c, r, g, b);
          }
          break;
        }
      }
    }
  }

  if (ferror(f)) {
    fop_error(fop, "Error reading file.\n");
    return false;
  }
  else {
    return true;
  }
}

bool PcxFormat::onSave(FileOp* fop)
{
  Image *image = fop->seq.image;
  int c, r, g, b;
  int x, y;
  int runcount;
  int depth, planes;
  char runchar;
  char ch = 0;

  FileHandle f(fop->filename.c_str(), "wb");

  if (image->getPixelFormat() == IMAGE_RGB) {
    depth = 24;
    planes = 3;
  }
  else {
    depth = 8;
    planes = 1;
  }

  fputc(10, f);                      /* manufacturer */
  fputc(5, f);                       /* version */
  fputc(1, f);                       /* run length encoding  */
  fputc(8, f);                       /* 8 bits per pixel */
  fputw(0, f);                       /* xmin */
  fputw(0, f);                       /* ymin */
  fputw(image->w-1, f);              /* xmax */
  fputw(image->h-1, f);              /* ymax */
  fputw(320, f);                     /* HDpi */
  fputw(200, f);                     /* VDpi */

  for (c=0; c<16; c++) {
    fop_sequence_get_color(fop, c, &r, &g, &b);
    fputc(r, f);
    fputc(g, f);
    fputc(b, f);
  }

  fputc(0, f);                      /* reserved */
  fputc(planes, f);                 /* one or three color planes */
  fputw(image->w, f);               /* number of bytes per scanline */
  fputw(1, f);                      /* color palette */
  fputw(image->w, f);               /* hscreen size */
  fputw(image->h, f);               /* vscreen size */
  for (c=0; c<54; c++)              /* filler */
    fputc(0, f);

  for (y=0; y<image->h; y++) {           /* for each scanline... */
    runcount = 0;
    runchar = 0;
    for (x=0; x<image->w*planes; x++) {  /* for each pixel... */
      if (depth == 8) {
        if (image->getPixelFormat() == IMAGE_INDEXED)
          ch = image_getpixel_fast<IndexedTraits>(image, x, y);
        else if (image->getPixelFormat() == IMAGE_GRAYSCALE) {
          c = image_getpixel_fast<GrayscaleTraits>(image, x, y);
          ch = _graya_getv(c);
        }
      }
      else {
        if (x < image->w) {
          c = image_getpixel_fast<RgbTraits>(image, x, y);
          ch = _rgba_getr(c);
        }
        else if (x<image->w*2) {
          c = image_getpixel_fast<RgbTraits>(image, x-image->w, y);
          ch = _rgba_getg(c);
        }
        else {
          c = image_getpixel_fast<RgbTraits>(image, x-image->w*2, y);
          ch = _rgba_getb(c);
        }
      }
      if (runcount == 0) {
        runcount = 1;
        runchar = ch;
      }
      else {
        if ((ch != runchar) || (runcount >= 0x3f)) {
          if ((runcount > 1) || ((runchar & 0xC0) == 0xC0))
            fputc(0xC0 | runcount, f);
          fputc(runchar, f);
          runcount = 1;
          runchar = ch;
        }
        else
          runcount++;
      }
    }

    if ((runcount > 1) || ((runchar & 0xC0) == 0xC0))
      fputc(0xC0 | runcount, f);

    fputc(runchar, f);

    fop_progress(fop, (float)(y+1) / (float)(image->h));
  }

  if (depth == 8) {                      /* 256 color palette */
    fputc(12, f);

    for (c=0; c<256; c++) {
      fop_sequence_get_color(fop, c, &r, &g, &b);
      fputc(r, f);
      fputc(g, f);
      fputc(b, f);
    }
  }

  if (ferror(f)) {
    fop_error(fop, "Error writing file.\n");
    return false;
  }
  else {
    return true;
  }
}

} // namespace app
