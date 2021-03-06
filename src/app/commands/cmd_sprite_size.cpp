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

#include "app/commands/command.h"
#include "app/context_access.h"
#include "app/document_api.h"
#include "app/find_widget.h"
#include "app/ini_file.h"
#include "app/job.h"
#include "app/load_widget.h"
#include "app/modules/gui.h"
#include "app/modules/palettes.h"
#include "app/ui_context.h"
#include "app/undo_transaction.h"
#include "base/bind.h"
#include "base/unique_ptr.h"
#include "raster/cel.h"
#include "raster/image.h"
#include "raster/mask.h"
#include "raster/sprite.h"
#include "raster/stock.h"
#include "ui/ui.h"

#include <allegro/unicode.h>

#define PERC_FORMAT     "%.1f"

namespace app {

using namespace ui;

class SpriteSizeJob : public Job {
  ContextWriter m_writer;
  Document* m_document;
  Sprite* m_sprite;
  int m_new_width;
  int m_new_height;
  ResizeMethod m_resize_method;

  inline int scale_x(int x) const { return x * m_new_width / m_sprite->getWidth(); }
  inline int scale_y(int y) const { return y * m_new_height / m_sprite->getHeight(); }

public:

  SpriteSizeJob(const ContextReader& reader, int new_width, int new_height, ResizeMethod resize_method)
    : Job("Sprite Size")
    , m_writer(reader)
    , m_document(m_writer.document())
    , m_sprite(m_writer.sprite())
  {
    m_new_width = new_width;
    m_new_height = new_height;
    m_resize_method = resize_method;
  }

protected:

  /**
   * [working thread]
   */
  virtual void onJob()
  {
    UndoTransaction undoTransaction(m_writer.context(), "Sprite Size");
    DocumentApi api = m_writer.document()->getApi();

    // Get all sprite cels
    CelList cels;
    m_sprite->getCels(cels);

    // For each cel...
    int progress = 0;
    for (CelIterator it = cels.begin(); it != cels.end(); ++it, ++progress) {
      Cel* cel = *it;

      // Change its location
      api.setCelPosition(m_sprite, cel, scale_x(cel->getX()), scale_y(cel->getY()));

      // Get cel's image
      Image* image = m_sprite->getStock()->getImage(cel->getImage());
      if (!image)
        continue;

      // Resize the image
      int w = scale_x(image->w);
      int h = scale_y(image->h);
      Image* new_image = Image::create(image->getPixelFormat(), MAX(1, w), MAX(1, h));

      image_fixup_transparent_colors(image);
      image_resize(image, new_image,
                   m_resize_method,
                   m_sprite->getPalette(cel->getFrame()),
                   m_sprite->getRgbMap(cel->getFrame()));

      api.replaceStockImage(m_sprite, cel->getImage(), new_image);

      jobProgress((float)progress / cels.size());

      // cancel all the operation?
      if (isCanceled())
        return;        // UndoTransaction destructor will undo all operations
    }

    // Resize mask
    if (m_document->isMaskVisible()) {
      base::UniquePtr<Image> old_bitmap
        (image_crop(m_document->getMask()->getBitmap(), -1, -1,
                    m_document->getMask()->getBitmap()->w+2,
                    m_document->getMask()->getBitmap()->h+2, 0));

      int w = scale_x(old_bitmap->w);
      int h = scale_y(old_bitmap->h);
      base::UniquePtr<Mask> new_mask(new Mask);
      new_mask->replace(scale_x(m_document->getMask()->getBounds().x-1),
                        scale_y(m_document->getMask()->getBounds().y-1), MAX(1, w), MAX(1, h));
      image_resize(old_bitmap, new_mask->getBitmap(),
                   m_resize_method,
                   m_sprite->getPalette(FrameNumber(0)), // Ignored
                   m_sprite->getRgbMap(FrameNumber(0))); // Ignored

      // Reshrink
      new_mask->intersect(new_mask->getBounds().x, new_mask->getBounds().y,
                          new_mask->getBounds().w, new_mask->getBounds().h);

      // Copy new mask
      api.copyToCurrentMask(new_mask);

      // Regenerate mask
      m_document->resetTransformation();
      m_document->generateMaskBoundaries();
    }

    // resize sprite
    api.setSpriteSize(m_sprite, m_new_width, m_new_height);

    // commit changes
    undoTransaction.commit();
  }

};

class SpriteSizeCommand : public Command {
public:
  SpriteSizeCommand();
  Command* clone() { return new SpriteSizeCommand(*this); }

protected:
  bool onEnabled(Context* context);
  void onExecute(Context* context);

private:
  void onLockRatioClick();
  void onWidthPxChange();
  void onHeightPxChange();
  void onWidthPercChange();
  void onHeightPercChange();

  CheckBox* m_lockRatio;
  Entry* m_widthPx;
  Entry* m_heightPx;
  Entry* m_widthPerc;
  Entry* m_heightPerc;
};

SpriteSizeCommand::SpriteSizeCommand()
  : Command("SpriteSize",
            "Sprite Size",
            CmdRecordableFlag)
{
}

bool SpriteSizeCommand::onEnabled(Context* context)
{
  return context->checkFlags(ContextFlags::ActiveDocumentIsWritable |
                             ContextFlags::HasActiveSprite);
}

void SpriteSizeCommand::onExecute(Context* context)
{
  const ContextReader reader(UIContext::instance()); // TODO use the context in sprite size command
  const Sprite* sprite(reader.sprite());

  // load the window widget
  base::UniquePtr<Window> window(app::load_widget<Window>("sprite_size.xml", "sprite_size"));
  m_widthPx = app::find_widget<Entry>(window, "width_px");
  m_heightPx = app::find_widget<Entry>(window, "height_px");
  m_widthPerc = app::find_widget<Entry>(window, "width_perc");
  m_heightPerc = app::find_widget<Entry>(window, "height_perc");
  m_lockRatio = app::find_widget<CheckBox>(window, "lock_ratio");
  ComboBox* method = app::find_widget<ComboBox>(window, "method");
  Widget* ok = app::find_widget<Widget>(window, "ok");

  m_widthPx->setTextf("%d", sprite->getWidth());
  m_heightPx->setTextf("%d", sprite->getHeight());

  m_lockRatio->Click.connect(Bind<void>(&SpriteSizeCommand::onLockRatioClick, this));
  m_widthPx->EntryChange.connect(Bind<void>(&SpriteSizeCommand::onWidthPxChange, this));
  m_heightPx->EntryChange.connect(Bind<void>(&SpriteSizeCommand::onHeightPxChange, this));
  m_widthPerc->EntryChange.connect(Bind<void>(&SpriteSizeCommand::onWidthPercChange, this));
  m_heightPerc->EntryChange.connect(Bind<void>(&SpriteSizeCommand::onHeightPercChange, this));

  method->addItem("Nearest-neighbor");
  method->addItem("Bilinear");
  method->setSelectedItemIndex(get_config_int("SpriteSize", "Method", RESIZE_METHOD_NEAREST_NEIGHBOR));

  window->remapWindow();
  window->centerWindow();

  load_window_pos(window, "SpriteSize");
  window->setVisible(true);
  window->openWindowInForeground();
  save_window_pos(window, "SpriteSize");

  if (window->getKiller() == ok) {
    int new_width = m_widthPx->getTextInt();
    int new_height = m_heightPx->getTextInt();
    ResizeMethod resize_method =
      (ResizeMethod)method->getSelectedItemIndex();

    set_config_int("SpriteSize", "Method", resize_method);

    {
      SpriteSizeJob job(reader, new_width, new_height, resize_method);
      job.startJob();
    }

    ContextWriter writer(reader);
    update_screen_for_document(writer.document());
  }
}

void SpriteSizeCommand::onLockRatioClick()
{
  const ContextReader reader(UIContext::instance()); // TODO use the context in sprite size command

  onWidthPxChange();
}

void SpriteSizeCommand::onWidthPxChange()
{
  const ContextReader reader(UIContext::instance()); // TODO use the context in sprite size command
  const Sprite* sprite(reader.sprite());
  int width = m_widthPx->getTextInt();
  double perc = 100.0 * width / sprite->getWidth();

  m_widthPerc->setTextf(PERC_FORMAT, perc);

  if (m_lockRatio->isSelected()) {
    m_heightPerc->setTextf(PERC_FORMAT, perc);
    m_heightPx->setTextf("%d", sprite->getHeight() * width / sprite->getWidth());
  }
}

void SpriteSizeCommand::onHeightPxChange()
{
  const ContextReader reader(UIContext::instance()); // TODO use the context in sprite size command
  const Sprite* sprite(reader.sprite());
  int height = m_heightPx->getTextInt();
  double perc = 100.0 * height / sprite->getHeight();

  m_heightPerc->setTextf(PERC_FORMAT, perc);

  if (m_lockRatio->isSelected()) {
    m_widthPerc->setTextf(PERC_FORMAT, perc);
    m_widthPx->setTextf("%d", sprite->getWidth() * height / sprite->getHeight());
  }
}

void SpriteSizeCommand::onWidthPercChange()
{
  const ContextReader reader(UIContext::instance()); // TODO use the context in sprite size command
  const Sprite* sprite(reader.sprite());
  double width = m_widthPerc->getTextDouble();

  m_widthPx->setTextf("%d", (int)(sprite->getWidth() * width / 100));

  if (m_lockRatio->isSelected()) {
    m_heightPx->setTextf("%d", (int)(sprite->getHeight() * width / 100));
    m_heightPerc->setText(m_widthPerc->getText());
  }
}

void SpriteSizeCommand::onHeightPercChange()
{
  const ContextReader reader(UIContext::instance()); // TODO use the context in sprite size command
  const Sprite* sprite(reader.sprite());
  double height = m_heightPerc->getTextDouble();

  m_heightPx->setTextf("%d", (int)(sprite->getHeight() * height / 100));

  if (m_lockRatio->isSelected()) {
    m_widthPx->setTextf("%d", (int)(sprite->getWidth() * height / 100));
    m_widthPerc->setText(m_heightPerc->getText());
  }
}

Command* CommandFactory::createSpriteSizeCommand()
{
  return new SpriteSizeCommand;
}

} // namespace app
