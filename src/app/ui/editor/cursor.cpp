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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/app.h"
#include "app/color.h"
#include "app/color_utils.h"
#include "app/ini_file.h"
#include "app/modules/editors.h"
#include "app/settings/settings.h"
#include "app/tools/ink.h"
#include "app/tools/tool.h"
#include "app/ui/editor/editor.h"
#include "app/ui_context.h"
#include "app/util/boundary.h"
#include "base/memory.h"
#include "raster/image.h"
#include "raster/layer.h"
#include "raster/pen.h"
#include "raster/sprite.h"
#include "ui/base.h"
#include "ui/rect.h"
#include "ui/system.h"
#include "ui/widget.h"

#include <algorithm>
#include <allegro.h>

#ifdef WIN32
#undef max
#undef min
#endif

namespace app {

using namespace ui;

/**********************************************************************/
/* drawing-cursor routines */
/**********************************************************************/

/**
 * Returns true if the cursor of the editor needs subpixel movement.
 */
#define IS_SUBPIXEL(editor)     ((editor)->m_zoom >= 2)

/**
 * Maximum quantity of colors to save pixels overlapped by the cursor.
 */
#define MAX_SAVED   4096

static struct {
  int pen_type;
  int pen_size;
  int pen_angle;
  int nseg;
  BoundSeg *seg;
} cursor_bound = { 0, 0, 0, 0, NULL };

enum {
  CURSOR_PENCIL      = 1,       // New cursor style (with preview)
  CURSOR_CROSS_ONE   = 2,       // Old cursor style (deprecated)
  CURSOR_BOUNDS      = 4        // Old cursor boundaries (deprecated)
};

static int cursor_type = CURSOR_PENCIL;
static int cursor_negative;

static int saved_pixel[MAX_SAVED];
static int saved_pixel_n;

// These clipping regions are shared between all editors, so we cannot
// make assumptions about their old state
static gfx::Region clipping_region;
static gfx::Region old_clipping_region;

static void generate_cursor_boundaries();

static void editor_cursor_pencil(Editor *editor, int x, int y, int color, int thickness, void (*pixel)(BITMAP *bmp, int x, int y, int color));
static void editor_cursor_cross(Editor *editor, int x, int y, int color, int thickness, void (*pixel)(BITMAP *bmp, int x, int y, int color));
static void editor_cursor_bounds(Editor *editor, int x, int y, int color, void (*pixel)(BITMAP *bmp, int x, int y, int color));

static void savepixel(BITMAP *bmp, int x, int y, int color);
static void drawpixel(BITMAP *bmp, int x, int y, int color);
static void cleanpixel(BITMAP *bmp, int x, int y, int color);

static int get_pen_color(Sprite* sprite, Layer* layer);

//////////////////////////////////////////////////////////////////////
// CURSOR COLOR
//////////////////////////////////////////////////////////////////////

static app::Color cursor_color;
static int _cursor_color;
static bool _cursor_mask;

static void update_cursor_color()
{
  if (ji_screen)
    _cursor_color = color_utils::color_for_allegro(cursor_color, bitmap_color_depth(ji_screen));
  else
    _cursor_color = 0;

  _cursor_mask = (cursor_color.getType() == app::Color::MaskType);
}

int Editor::get_raw_cursor_color()
{
  return _cursor_color;
}

bool Editor::is_cursor_mask()
{
  return _cursor_mask;
}

app::Color Editor::get_cursor_color()
{
  return cursor_color;
}

void Editor::set_cursor_color(const app::Color& color)
{
  cursor_color = color;
  update_cursor_color();
}

//////////////////////////////////////////////////////////////////////
// Slots for App signals
//////////////////////////////////////////////////////////////////////

static int pen_size_thick = 0;
static Pen* current_pen = NULL;

static void on_palette_change_update_cursor_color()
{
  update_cursor_color();
}

static void on_pen_before_change()
{
  if (current_editor != NULL) {
    pen_size_thick = current_editor->getCursorThick();
    if (pen_size_thick)
      current_editor->hideDrawingCursor();
  }
}

static void on_pen_after_change()
{
  if (current_editor != NULL) {
    // Show drawing cursor
    if (current_editor->getSprite() && pen_size_thick > 0)
      current_editor->showDrawingCursor();
  }
}

static Pen* editor_get_current_pen()
{
  // Create the current pen from settings
  tools::Tool* current_tool = UIContext::instance()
    ->getSettings()
    ->getCurrentTool();

  IPenSettings* pen_settings = UIContext::instance()
    ->getSettings()
    ->getToolSettings(current_tool)
    ->getPen();

  ASSERT(pen_settings != NULL);

  if (!current_pen ||
      current_pen->get_type() != pen_settings->getType() ||
      current_pen->get_size() != pen_settings->getSize() ||
      current_pen->get_angle() != pen_settings->getAngle()) {
    delete current_pen;
    current_pen = new Pen(pen_settings->getType(),
                          pen_settings->getSize(),
                          pen_settings->getAngle());
  }

  return current_pen;
}

//////////////////////////////////////////////////////////////////////
// CURSOR
//////////////////////////////////////////////////////////////////////

void Editor::editor_cursor_init()
{
  // Cursor color
  set_cursor_color(get_config_color("Tools", "CursorColor", app::Color::fromMask()));

  App::instance()->PaletteChange.connect(&on_palette_change_update_cursor_color);
  App::instance()->PenSizeBeforeChange.connect(&on_pen_before_change);
  App::instance()->PenSizeAfterChange.connect(&on_pen_after_change);
  App::instance()->PenAngleBeforeChange.connect(&on_pen_before_change);
  App::instance()->PenAngleAfterChange.connect(&on_pen_after_change);
}

void Editor::editor_cursor_exit()
{
  set_config_color("Tools", "CursorColor", cursor_color);

  if (cursor_bound.seg != NULL)
    base_free(cursor_bound.seg);

  delete current_pen;
  current_pen = NULL;
}

/**
 * Draws the pen cursor inside the specified editor.
 *
 * @warning You should clean the cursor before to use
 * this routine with other editor.
 *
 * @param widget The editor widget
 * @param x Absolute position in X axis of the mouse.
 * @param y Absolute position in Y axis of the mouse.
 *
 * @see editor_clean_cursor
 */
void Editor::editor_draw_cursor(int x, int y, bool refresh)
{
  ASSERT(m_cursor_thick == 0);
  ASSERT(m_sprite != NULL);

  // Get drawable region
  getDrawableRegion(clipping_region, kCutTopWindows);

  /* get cursor color */
  cursor_negative = is_cursor_mask();
  int color = get_raw_cursor_color();

  /* cursor in the screen (view) */
  m_cursor_screen_x = x;
  m_cursor_screen_y = y;

  /* get cursor position in the editor */
  screenToEditor(x, y, &x, &y);

  // Get the current tool
  tools::Tool* current_tool = UIContext::instance()
    ->getSettings()
    ->getCurrentTool();

  // Setup the cursor type depending the current tool
  if (current_tool->getInk(0)->isSelection()) {
    cursor_type = CURSOR_CROSS_ONE;
  }
  else if (// Use cursor bounds for inks that are effects (eraser, blur, etc.)
           current_tool->getInk(0)->isEffect() ||
           // or when the FG color is mask and we are not in the background layer
           (UIContext::instance()->getSettings()->getFgColor().getType() == app::Color::MaskType &&
            (m_layer != NULL && !m_layer->isBackground()))) {
    cursor_type = CURSOR_BOUNDS;
  }
  else {
    cursor_type = CURSOR_PENCIL;
  }

  // For cursor type 'bounds' we have to generate cursor boundaries
  if (cursor_type & CURSOR_BOUNDS)
    generate_cursor_boundaries();

  // draw pixel/pen preview
  if (cursor_type & CURSOR_PENCIL && m_state->requirePenPreview()) {
    IToolSettings* tool_settings = UIContext::instance()
      ->getSettings()
      ->getToolSettings(current_tool);

    int pen_color = get_pen_color(m_sprite, m_layer);
    uint32_t new_mask_color;
    Pen* pen = editor_get_current_pen();
    gfx::Rect penBounds = pen->getBounds();

    // Create the extra cel to show the pen preview
    m_document->prepareExtraCel(x+penBounds.x, y+penBounds.y,
                                penBounds.w, penBounds.h,
                                tool_settings->getOpacity());

    // In 'indexed' images, if the current color is 0, we have to use
    // a different mask color (different from 0) to draw the extra layer
    if (m_sprite->getPixelFormat() == IMAGE_INDEXED && pen_color == 0) {
      new_mask_color = 1;
    }
    else {
      new_mask_color = 0;
    }

    Image* extraImage = m_document->getExtraCelImage();
    if (extraImage->mask_color != new_mask_color)
      image_clear(extraImage, extraImage->mask_color = new_mask_color);
    image_putpen(extraImage, pen, -penBounds.x, -penBounds.y,
                 pen_color, extraImage->mask_color);

    if (refresh) {
      m_document->notifySpritePixelsModified
        (m_sprite,
         gfx::Region(gfx::Rect(x+penBounds.x,
                               y+penBounds.y,
                               penBounds.w, penBounds.h)));
    }
  }

  /* save area and draw the cursor */
  if (refresh) {
    acquire_bitmap(ji_screen);
    ji_screen->clip = false;
    for_each_pixel_of_pen(m_cursor_screen_x, m_cursor_screen_y, x, y, color, savepixel);
    for_each_pixel_of_pen(m_cursor_screen_x, m_cursor_screen_y, x, y, color, drawpixel);
    ji_screen->clip = true;
    release_bitmap(ji_screen);
  }

  // cursor thickness
  m_cursor_thick = 1; // get_thickness_for_cursor();

  /* cursor in the editor (model) */
  m_cursor_editor_x = x;
  m_cursor_editor_y = y;

  // Save the clipping-region to know where to clean the pixels
  old_clipping_region = clipping_region;
}

void Editor::editor_move_cursor(int x, int y, bool refresh)
{
  ASSERT(m_sprite != NULL);

  int old_screen_x = m_cursor_screen_x;
  int old_screen_y = m_cursor_screen_y;
  int old_x = m_cursor_editor_x;
  int old_y = m_cursor_editor_y;

  editor_clean_cursor(false);
  editor_draw_cursor(x, y, false);

  int new_x = m_cursor_editor_x;
  int new_y = m_cursor_editor_y;

  if (refresh) {
    /* restore points */
    acquire_bitmap(ji_screen);
    ji_screen->clip = FALSE;
    for_each_pixel_of_pen(old_screen_x, old_screen_y, old_x, old_y, 0, cleanpixel);
    ji_screen->clip = TRUE;
    release_bitmap(ji_screen);

    if (cursor_type & CURSOR_PENCIL && m_state->requirePenPreview()) {
      Pen* pen = editor_get_current_pen();
      gfx::Rect penBounds = pen->getBounds();
      gfx::Rect rc1(old_x+penBounds.x, old_y+penBounds.y, penBounds.w, penBounds.h);
      gfx::Rect rc2(new_x+penBounds.x, new_y+penBounds.y, penBounds.w, penBounds.h);
      m_document->notifySpritePixelsModified
        (m_sprite, gfx::Region(rc1.createUnion(rc2)));
    }

    /* save area and draw the cursor */
    int color = get_raw_cursor_color();
    acquire_bitmap(ji_screen);
    ji_screen->clip = false;
    for_each_pixel_of_pen(m_cursor_screen_x, m_cursor_screen_y, new_x, new_y, color, savepixel);
    for_each_pixel_of_pen(m_cursor_screen_x, m_cursor_screen_y, new_x, new_y, color, drawpixel);
    ji_screen->clip = true;
    release_bitmap(ji_screen);
  }
}

/**
 * Cleans the pen cursor from the specified editor.
 *
 * The mouse position is got from the last
 * call to @c editor_draw_cursor. So you must
 * to use this routine only if you called
 * @c editor_draw_cursor before with the specified
 * editor @a widget.
 *
 * @param widget The editor widget
 *
 * @see editor_draw_cursor
 */
void Editor::editor_clean_cursor(bool refresh)
{
  int x, y;

  ASSERT(m_cursor_thick != 0);
  ASSERT(m_sprite != NULL);

  getDrawableRegion(clipping_region, kCutTopWindows);

  x = m_cursor_editor_x;
  y = m_cursor_editor_y;

  if (refresh) {
    /* restore points */
    acquire_bitmap(ji_screen);
    ji_screen->clip = FALSE;
    for_each_pixel_of_pen(m_cursor_screen_x, m_cursor_screen_y, x, y, 0, cleanpixel);
    ji_screen->clip = TRUE;
    release_bitmap(ji_screen);
  }

  // clean pixel/pen preview
  if (cursor_type & CURSOR_PENCIL && m_state->requirePenPreview()) {
    Pen* pen = editor_get_current_pen();
    gfx::Rect penBounds = pen->getBounds();

    m_document->prepareExtraCel(x+penBounds.x, y+penBounds.y,
                                penBounds.w, penBounds.h,
                                0); // Opacity = 0

    if (refresh) {
      m_document->notifySpritePixelsModified
        (m_sprite,
         gfx::Region(gfx::Rect(x+penBounds.x,
                               y+penBounds.y,
                               penBounds.w, penBounds.h)));
    }
  }

  m_cursor_thick = 0;

  clipping_region.clear();
  old_clipping_region.clear();
}

/**
 * Returns true if the cursor to draw in the editor has subpixel
 * movement (a little pixel of the screen that indicates where is the
 * mouse inside the pixel of the sprite).
 */
bool Editor::editor_cursor_is_subpixel()
{
  return IS_SUBPIXEL(this);
}

//////////////////////////////////////////////////////////////////////

static void generate_cursor_boundaries()
{
  tools::Tool* current_tool = UIContext::instance()
    ->getSettings()
    ->getCurrentTool();

  IPenSettings* pen_settings = NULL;
  if (current_tool)
    pen_settings = UIContext::instance()
      ->getSettings()
      ->getToolSettings(current_tool)
      ->getPen();

  if (cursor_bound.seg == NULL ||
      cursor_bound.pen_type != pen_settings->getType() ||
      cursor_bound.pen_size != pen_settings->getSize() ||
      cursor_bound.pen_angle != pen_settings->getAngle()) {
    cursor_bound.pen_type = pen_settings->getType();
    cursor_bound.pen_size = pen_settings->getSize();
    cursor_bound.pen_angle = pen_settings->getAngle();

    if (cursor_bound.seg != NULL)
      base_free(cursor_bound.seg);

    Pen* pen;

    if (pen_settings) {
      pen = new Pen(pen_settings->getType(),
                    pen_settings->getSize(),
                    pen_settings->getAngle());
    }
    else
      pen = new Pen();

    cursor_bound.seg = find_mask_boundary(pen->get_image(),
                                          &cursor_bound.nseg,
                                          IgnoreBounds, 0, 0, 0, 0);
    delete pen;
  }
}

void Editor::for_each_pixel_of_pen(int screen_x, int screen_y,
                                   int sprite_x, int sprite_y, int color,
                                   void (*pixel)(BITMAP *bmp, int x, int y, int color))
{
  saved_pixel_n = 0;

  if (cursor_type & CURSOR_PENCIL) {
    editor_cursor_pencil(this, screen_x, screen_y, color, 1, pixel);
  }

  if (cursor_type & CURSOR_CROSS_ONE) {
    editor_cursor_cross(this, sprite_x, sprite_y, color, 1, pixel);
  }

  if (cursor_type & CURSOR_BOUNDS) {
    editor_cursor_bounds(this, sprite_x, sprite_y, color, pixel);
  }

  if (IS_SUBPIXEL(this)) {
    (*pixel)(ji_screen, screen_x, screen_y, color);
  }
}

//////////////////////////////////////////////////////////////////////
// New cross

static void editor_cursor_pencil(Editor *editor, int x, int y, int color, int thickness, void (*pixel)(BITMAP *bmp, int x, int y, int color))
{
  static int cursor_cross[7*7] = {
    0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 0, 1, 1,
    0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0,
  };
  int u, v, xout, yout;

  for (v=0; v<7; v++) {
    for (u=0; u<7; u++) {
      if (cursor_cross[v*7+u]) {
        xout = x-3+u;
        yout = y-3+v;
        (*pixel)(ji_screen, xout, yout, color);
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////
// Old cross

static void editor_cursor_cross(Editor* editor, int x, int y, int color, int thickness,
                                void (*pixel)(BITMAP *bmp, int x, int y, int color))
{
  static int cursor_cross[6*6] = {
    0, 0, 1, 1, 0, 0,
    0, 0, 1, 1, 0, 0,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    0, 0, 1, 1, 0, 0,
    0, 0, 1, 1, 0, 0,
  };
  int u, v, xout, yout;
  int zoom = editor->getZoom();

  for (v=0; v<6; v++) {
    for (u=0; u<6; u++) {
      if (cursor_cross[v*6+u]) {
        editor->editorToScreen(x, y, &xout, &yout);

        xout += ((u<3) ?
                 u-((thickness>>1)<<zoom)-3:
                 u-((thickness>>1)<<zoom)-3+(thickness<<zoom));

        yout += ((v<3)?
                 v-((thickness>>1)<<zoom)-3:
                 v-((thickness>>1)<<zoom)-3+(thickness<<zoom));

        (*pixel)(ji_screen, xout, yout, color);
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////
// Cursor Bounds

static void editor_cursor_bounds(Editor *editor, int x, int y, int color, void (*pixel) (BITMAP *bmp, int x, int y, int color))
{
  int c, x1, y1, x2, y2;
  BoundSeg *seg;

  for (c=0; c<cursor_bound.nseg; c++) {
    seg = cursor_bound.seg+c;

    x1 = seg->x1 - cursor_bound.pen_size/2;
    y1 = seg->y1 - cursor_bound.pen_size/2;
    x2 = seg->x2 - cursor_bound.pen_size/2;
    y2 = seg->y2 - cursor_bound.pen_size/2;

    editor->editorToScreen(x+x1, y+y1, &x1, &y1);
    editor->editorToScreen(x+x2, y+y2, &x2, &y2);

    if (seg->open) {            /* outside */
      if (x1 == x2) {
        x1--;
        x2--;
        y2--;
      }
      else {
        y1--;
        y2--;
        x2--;
      }
    }
    else {
      if (x1 == x2) {
        y2--;
      }
      else {
        x2--;
      }
    }

    do_line(ji_screen, x1, y1, x2, y2, color, pixel);
  }
}

//////////////////////////////////////////////////////////////////////
// Helpers

static void savepixel(BITMAP *bmp, int x, int y, int color)
{
  if (saved_pixel_n < MAX_SAVED && clipping_region.contains(gfx::Point(x, y)))
    saved_pixel[saved_pixel_n++] = getpixel(bmp, x, y);
}

static void drawpixel(BITMAP *bmp, int x, int y, int color)
{
  if (saved_pixel_n < MAX_SAVED && clipping_region.contains(gfx::Point(x, y))) {
    if (cursor_negative) {
      int r, g, b, c = saved_pixel[saved_pixel_n++];

      r = getr(c);
      g = getg(c);
      b = getb(c);

      putpixel(bmp, x, y, ui::to_system(color_utils::blackandwhite_neg(ui::rgba(r, g, b))));
    }
    else {
      putpixel(bmp, x, y, color);
    }
  }
}

static void cleanpixel(BITMAP *bmp, int x, int y, int color)
{
  if (saved_pixel_n < MAX_SAVED) {
    if (clipping_region.contains(gfx::Point(x, y)))
      putpixel(bmp, x, y, saved_pixel[saved_pixel_n++]);
    else if (!old_clipping_region.isEmpty() &&
             old_clipping_region.contains(gfx::Point(x, y)))
      saved_pixel_n++;
  }
}

static int get_pen_color(Sprite* sprite, Layer* layer)
{
  app::Color c = UIContext::instance()->getSettings()->getFgColor();
  ASSERT(sprite != NULL);

  // Avoid using invalid colors
  if (!c.isValid())
    return 0;

  if (layer != NULL)
    return color_utils::color_for_layer(c, layer);
  else
    return color_utils::color_for_image(c, sprite->getPixelFormat());
}

} // namespace app
