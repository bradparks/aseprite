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

#include "app/ui/toolbar.h"

#include "app/app.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/modules/editors.h"
#include "app/modules/gfx.h"
#include "app/modules/gui.h"
#include "app/settings/settings.h"
#include "app/tools/tool_box.h"
#include "app/ui/main_window.h"
#include "app/ui/mini_editor.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/status_bar.h"
#include "app/ui_context.h"
#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/signal.h"
#include "gfx/size.h"
#include "ui/ui.h"

#include <allegro.h>
#include <string>

namespace app {

using namespace app::skin;
using namespace gfx;
using namespace ui;
using namespace tools;

// Class to show a group of tools (horizontally)
// This widget is inside the ToolBar::m_popupWindow
class ToolStrip : public Widget {
public:
  ToolStrip(ToolGroup* group, ToolBar* toolbar);
  ~ToolStrip();

  void saveOverlappedArea(const Rect& bounds);

  Signal1<void, Tool*> ToolSelected;

protected:
  bool onProcessMessage(Message* msg) OVERRIDE;
  void onPreferredSize(PreferredSizeEvent& ev) OVERRIDE;

private:
  Rect getToolBounds(int index);

  ToolGroup* m_group;
  Tool* m_hot_tool;
  ToolBar* m_toolbar;
  BITMAP* m_overlapped;
};

static Size getToolIconSize(Widget* widget)
{
  SkinTheme* theme = static_cast<SkinTheme*>(widget->getTheme());
  BITMAP* icon = theme->get_toolicon("configuration");
  if (icon)
    return Size(icon->w, icon->h);
  else
    return Size(16, 16) * jguiscale();
}

//////////////////////////////////////////////////////////////////////
// ToolBar

ToolBar* ToolBar::m_instance = NULL;

ToolBar::ToolBar()
  : Widget(kGenericWidget)
  , m_tipTimer(300, this)
{
  m_instance = this;

  this->border_width.l = 1*jguiscale();
  this->border_width.t = 0;
  this->border_width.r = 1*jguiscale();
  this->border_width.b = 0;

  m_hot_tool = NULL;
  m_hot_index = NoneIndex;
  m_open_on_hot = false;
  m_popupWindow = NULL;
  m_tipWindow = NULL;
  m_tipOpened = false;

  ToolBox* toolbox = App::instance()->getToolBox();
  for (ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
    Tool* tool = *it;
    if (m_selected_in_group.find(tool->getGroup()) == m_selected_in_group.end())
      m_selected_in_group[tool->getGroup()] = tool;
  }
}

ToolBar::~ToolBar()
{
  delete m_popupWindow;
  delete m_tipWindow;
}

bool ToolBar::isToolVisible(Tool* tool)
{
  return (m_selected_in_group[tool->getGroup()] == tool);
}

bool ToolBar::onProcessMessage(Message* msg)
{
  switch (msg->type()) {

    case kPaintMessage: {
      const gfx::Rect& drawRect = static_cast<PaintMessage*>(msg)->rect();
      BITMAP *doublebuffer = create_bitmap(drawRect.w, drawRect.h);
      SkinTheme* theme = static_cast<SkinTheme*>(this->getTheme());
      ui::Color normalFace = theme->getColor(ThemeColor::ButtonNormalFace);
      ui::Color hotFace = theme->getColor(ThemeColor::ButtonHotFace);
      ToolBox* toolbox = App::instance()->getToolBox();
      ToolGroupList::iterator it = toolbox->begin_group();
      int groups = toolbox->getGroupsCount();
      Rect toolrc;

      clear_to_color(doublebuffer, to_system(theme->getColor(ThemeColor::TabSelectedFace)));

      for (int c=0; c<groups; ++c, ++it) {
        ToolGroup* tool_group = *it;
        Tool* tool = m_selected_in_group[tool_group];
        ui::Color face;
        int nw;

        if (UIContext::instance()->getSettings()->getCurrentTool() == tool ||
            m_hot_index == c) {
          nw = PART_TOOLBUTTON_HOT_NW;
          face = hotFace;
        }
        else {
          nw = c >= 0 && c < groups-1 ? PART_TOOLBUTTON_NORMAL_NW:
                                        PART_TOOLBUTTON_LAST_NW;
          face = normalFace;
        }

        toolrc = getToolGroupBounds(c);
        toolrc.offset(-drawRect.x, -drawRect.y);
        theme->draw_bounds_nw(doublebuffer, toolrc, nw, face);

        // Draw the tool icon
        BITMAP* icon = theme->get_toolicon(tool->getId().c_str());
        if (icon) {
          set_alpha_blender();
          draw_trans_sprite(doublebuffer, icon,
                            toolrc.x+toolrc.w/2-icon->w/2,
                            toolrc.y+toolrc.h/2-icon->h/2);
        }
      }

      // Draw button to show tool configuration
      toolrc = getToolGroupBounds(ConfigureToolIndex);
      toolrc.offset(-drawRect.x, -drawRect.y);
      bool isHot = (m_hot_index == ConfigureToolIndex);
      theme->draw_bounds_nw(doublebuffer,
                            toolrc,
                            isHot ? PART_TOOLBUTTON_HOT_NW:
                                    PART_TOOLBUTTON_LAST_NW,
                            isHot ? hotFace: normalFace);

      BITMAP* icon = theme->get_toolicon("configuration");
      if (icon) {
        set_alpha_blender();
        draw_trans_sprite(doublebuffer, icon,
                          toolrc.x+toolrc.w/2-icon->w/2,
                          toolrc.y+toolrc.h/2-icon->h/2);
      }

      // Draw button to show/hide mini editor
      toolrc = getToolGroupBounds(MiniEditorVisibilityIndex);
      toolrc.offset(-drawRect.x, -drawRect.y);
      isHot = (m_hot_index == MiniEditorVisibilityIndex ||
               App::instance()->getMainWindow()->getMiniEditor()->isMiniEditorEnabled());
      theme->draw_bounds_nw(doublebuffer,
                            toolrc,
                            isHot ? PART_TOOLBUTTON_HOT_NW:
                                    PART_TOOLBUTTON_LAST_NW,
                            isHot ? hotFace: normalFace);

      icon = theme->get_toolicon("minieditor");
      if (icon) {
        set_alpha_blender();
        draw_trans_sprite(doublebuffer, icon,
                          toolrc.x+toolrc.w/2-icon->w/2,
                          toolrc.y+toolrc.h/2-icon->h/2);
      }

      // Blit result to screen
      blit(doublebuffer, ji_screen, 0, 0,
           drawRect.x,
           drawRect.y,
           doublebuffer->w,
           doublebuffer->h);
      destroy_bitmap(doublebuffer);
      return true;
    }

    case kMouseDownMessage: {
      MouseMessage* mouseMsg = static_cast<MouseMessage*>(msg);
      ToolBox* toolbox = App::instance()->getToolBox();
      int groups = toolbox->getGroupsCount();
      Rect toolrc;

      closeTipWindow();

      ToolGroupList::iterator it = toolbox->begin_group();

      for (int c=0; c<groups; ++c, ++it) {
        ToolGroup* tool_group = *it;
        Tool* tool = m_selected_in_group[tool_group];

        toolrc = getToolGroupBounds(c);
        if (mouseMsg->position().y >= toolrc.y &&
            mouseMsg->position().y < toolrc.y+toolrc.h) {
          UIContext::instance()->getSettings()->setCurrentTool(tool);
          invalidate();

          openPopupWindow(c, tool_group);
        }
      }

      toolrc = getToolGroupBounds(ConfigureToolIndex);
      if (mouseMsg->position().y >= toolrc.y &&
          mouseMsg->position().y < toolrc.y+toolrc.h) {
        Command* conf_tools_cmd =
          CommandsModule::instance()->getCommandByName(CommandId::ConfigureTools);

        UIContext::instance()->executeCommand(conf_tools_cmd);
      }

      toolrc = getToolGroupBounds(MiniEditorVisibilityIndex);
      if (mouseMsg->position().y >= toolrc.y &&
          mouseMsg->position().y < toolrc.y+toolrc.h) {
        // Switch the state of the mini editor
        MiniEditorWindow* miniEditorWindow =
          App::instance()->getMainWindow()->getMiniEditor();
        bool state = miniEditorWindow->isMiniEditorEnabled();
        miniEditorWindow->setMiniEditorEnabled(!state);
      }
      break;
    }

    case kMouseMoveMessage: {
      MouseMessage* mouseMsg = static_cast<MouseMessage*>(msg);
      ToolBox* toolbox = App::instance()->getToolBox();
      int groups = toolbox->getGroupsCount();
      Tool* new_hot_tool = NULL;
      int new_hot_index = NoneIndex;
      Rect toolrc;

      ToolGroupList::iterator it = toolbox->begin_group();

      for (int c=0; c<groups; ++c, ++it) {
        ToolGroup* tool_group = *it;
        Tool* tool = m_selected_in_group[tool_group];

        toolrc = getToolGroupBounds(c);
        if (mouseMsg->position().y >= toolrc.y &&
            mouseMsg->position().y < toolrc.y+toolrc.h) {
          new_hot_tool = tool;
          new_hot_index = c;

          if ((m_open_on_hot) && (m_hot_tool != new_hot_tool))
            openPopupWindow(c, tool_group);
          break;
        }
      }

      toolrc = getToolGroupBounds(ConfigureToolIndex);
      if (mouseMsg->position().y >= toolrc.y &&
          mouseMsg->position().y < toolrc.y+toolrc.h) {
        new_hot_index = ConfigureToolIndex;
      }

      toolrc = getToolGroupBounds(MiniEditorVisibilityIndex);
      if (mouseMsg->position().y >= toolrc.y &&
          mouseMsg->position().y < toolrc.y+toolrc.h) {
        new_hot_index = MiniEditorVisibilityIndex;
      }

      // hot button changed
      if (new_hot_tool != m_hot_tool ||
          new_hot_index != m_hot_index) {
        m_hot_tool = new_hot_tool;
        m_hot_index = new_hot_index;
        invalidate();

        if (m_hot_index != NoneIndex)
          openTipWindow(m_hot_index, m_hot_tool);
        else
          closeTipWindow();

        if (m_hot_tool)
          StatusBar::instance()->showTool(0, m_hot_tool);
      }
      break;
    }

    case kMouseLeaveMessage:
      closeTipWindow();

      if (!m_popupWindow)
        m_tipOpened = false;

      m_hot_tool = NULL;
      m_hot_index = NoneIndex;
      invalidate();

      StatusBar::instance()->clearText();
      break;

    case kTimerMessage:
      if (static_cast<TimerMessage*>(msg)->timer() == &m_tipTimer) {
        if (m_tipWindow)
          m_tipWindow->openWindow();

        m_tipTimer.stop();
        m_tipOpened = true;
      }
      break;

  }

  return Widget::onProcessMessage(msg);
}

void ToolBar::onPreferredSize(PreferredSizeEvent& ev)
{
  Size iconsize = getToolIconSize(this);
  iconsize.w += this->border_width.l + this->border_width.r;
  iconsize.h += this->border_width.t + this->border_width.b;
  ev.setPreferredSize(iconsize);
}

int ToolBar::getToolGroupIndex(ToolGroup* group)
{
  ToolBox* toolbox = App::instance()->getToolBox();
  ToolGroupList::iterator it = toolbox->begin_group();
  int groups = toolbox->getGroupsCount();

  for (int c=0; c<groups; ++c, ++it) {
    if (group == *it)
      return c;
  }

  return -1;
}

void ToolBar::openPopupWindow(int group_index, ToolGroup* tool_group)
{
  // Close the current popup window
  if (m_popupWindow) {
    m_popupWindow->closeWindow(NULL);
    delete m_popupWindow;
    m_popupWindow = NULL;
  }

  // Close tip window
  closeTipWindow();

  // If this group contains only one tool, do not show the popup
  ToolBox* toolbox = App::instance()->getToolBox();
  int count = 0;
  for (ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
    Tool* tool = *it;
    if (tool->getGroup() == tool_group)
      ++count;
  }
  if (count <= 1)
    return;

  // In case this tool contains more than just one tool, show the popup window
  m_open_on_hot = true;
  m_popupWindow = new PopupWindow(NULL, false);
  m_popupWindow->Close.connect(Bind<void, ToolBar, ToolBar>(&ToolBar::onClosePopup, this));

  ToolStrip* toolstrip = new ToolStrip(tool_group, this);
  m_popupWindow->addChild(toolstrip);

  Rect rc = getToolGroupBounds(group_index);
  int w = 0;

  for (ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
    Tool* tool = *it;
    if (tool->getGroup() == tool_group)
      w += jrect_w(this->rc)-this->border_width.l-this->border_width.r-1;
  }

  rc.x -= w;
  rc.w = w;

  // Redraw the overlapped area and save it to use it in the ToolStrip::onProcessMessage(kPaintMessage)
  {
    getManager()->invalidateRect(rc);

    // Flush kPaintMessage messages and send them
    getManager()->flushRedraw();
    getManager()->dispatchMessages();

    // Save the area
    toolstrip->saveOverlappedArea(rc);
  }

  // Set hotregion of popup window
  Region rgn(rc);
  rgn.createUnion(rgn, Region(getBounds()));
  m_popupWindow->setHotRegion(rgn);

  m_popupWindow->setAutoRemap(false);
  m_popupWindow->setBounds(rc);
  toolstrip->setBounds(rc);
  m_popupWindow->openWindow();

  toolstrip->setBounds(rc);
}

Rect ToolBar::getToolGroupBounds(int group_index)
{
  ToolBox* toolbox = App::instance()->getToolBox();
  int groups = toolbox->getGroupsCount();
  Size iconsize = getToolIconSize(this);
  Rect rc(getBounds());
  rc.shrink(getBorder());

  switch (group_index) {

    case ConfigureToolIndex:
      rc.y += groups*(iconsize.h-1*jguiscale())+ 8*jguiscale();
      rc.h = iconsize.h+2*jguiscale();
      break;

    case MiniEditorVisibilityIndex:
      rc.y += rc.h - iconsize.h - 2*jguiscale();
      rc.h = iconsize.h+2*jguiscale();
      break;

    default:
      rc.y += group_index*(iconsize.h-1*jguiscale());
      rc.h = group_index < groups-1 ? iconsize.h+1*jguiscale():
                                      iconsize.h+2*jguiscale();
      break;
  }

  return rc;
}

Point ToolBar::getToolPositionInGroup(int group_index, Tool* tool)
{
  ToolBox* toolbox = App::instance()->getToolBox();
  int groups = toolbox->getGroupsCount();
  Size iconsize = getToolIconSize(this);
  int nth = 0;

  for (ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
    if (tool == *it)
      break;

    if ((*it)->getGroup() == tool->getGroup()) {
      ++nth;
    }
  }

  return Point(iconsize.w/2+iconsize.w*nth, iconsize.h);
}

void ToolBar::openTipWindow(ToolGroup* tool_group, Tool* tool)
{
  openTipWindow(getToolGroupIndex(tool_group), tool);
}

void ToolBar::openTipWindow(int group_index, Tool* tool)
{
  if (m_tipWindow)
    closeTipWindow();

  std::string tooltip;
  if (tool && group_index >= 0) {
    tooltip = tool->getText();
    if (tool->getTips().size() > 0) {
      tooltip += ":\n";
      tooltip += tool->getTips();
    }

    // Tool shortcut
    Accelerator* accel = get_accel_to_change_tool(tool);
    if (accel) {
      tooltip += "\n\nShortcut: ";
      tooltip += accel->toString();
    }
  }
  else if (group_index == ConfigureToolIndex) {
    tooltip = "Configure Tool";
  }
  else if (group_index == MiniEditorVisibilityIndex) {
    if (App::instance()->getMainWindow()->getMiniEditor()->isMiniEditorEnabled())
      tooltip = "Disable Mini-Editor";
    else
      tooltip = "Enable Mini-Editor";
  }
  else
    return;

  m_tipWindow = new TipWindow(tooltip.c_str(), true);
  m_tipWindow->setArrowAlign(JI_TOP | JI_RIGHT);
  m_tipWindow->remapWindow();

  Rect toolrc = getToolGroupBounds(group_index);
  Point arrow = tool ? getToolPositionInGroup(group_index, tool): Point(0, 0);
  int w = jrect_w(m_tipWindow->rc);
  int h = jrect_h(m_tipWindow->rc);
  int x = toolrc.x - w + (tool && m_popupWindow && m_popupWindow->isVisible() ? arrow.x-m_popupWindow->getBounds().w: 0);
  int y = toolrc.y + toolrc.h;

  m_tipWindow->positionWindow(MID(0, x, JI_SCREEN_W-w),
                              MID(0, y, JI_SCREEN_H-h));

  if (m_tipOpened)
    m_tipWindow->openWindow();
  else
    m_tipTimer.start();
}

void ToolBar::closeTipWindow()
{
  m_tipTimer.stop();

  if (m_tipWindow) {
    m_tipWindow->closeWindow(NULL);
    delete m_tipWindow;
    m_tipWindow = NULL;

    // Flush kPaintMessage messages and send them
    getManager()->flushRedraw();
    getManager()->dispatchMessages();
  }
}

void ToolBar::selectTool(Tool* tool)
{
  ASSERT(tool != NULL);

  m_selected_in_group[tool->getGroup()] = tool;

  UIContext::instance()->getSettings()->setCurrentTool(tool);
  invalidate();
}

void ToolBar::onClosePopup()
{
  closeTipWindow();

  if (!hasMouse())
    m_tipOpened = false;

  m_open_on_hot = false;
  m_hot_tool = NULL;
  invalidate();
}

//////////////////////////////////////////////////////////////////////
// ToolStrip
//////////////////////////////////////////////////////////////////////

ToolStrip::ToolStrip(ToolGroup* group, ToolBar* toolbar)
  : Widget(kGenericWidget)
{
  m_group = group;
  m_hot_tool = NULL;
  m_toolbar = toolbar;
  m_overlapped = NULL;
}

ToolStrip::~ToolStrip()
{
  if (m_overlapped)
    destroy_bitmap(m_overlapped);
}

void ToolStrip::saveOverlappedArea(const Rect& bounds)
{
  if (m_overlapped)
    destroy_bitmap(m_overlapped);

  m_overlapped = create_bitmap(bounds.w, bounds.h);

  blit(ji_screen, m_overlapped,
       bounds.x, bounds.y, 0, 0,
       bounds.w, bounds.h);
}

bool ToolStrip::onProcessMessage(Message* msg)
{
  switch (msg->type()) {

    case kPaintMessage: {
      gfx::Rect paintarea = static_cast<PaintMessage*>(msg)->rect();
      BITMAP *doublebuffer = create_bitmap(paintarea.w, paintarea.h);
      SkinTheme* theme = static_cast<SkinTheme*>(getTheme());
      ToolBox* toolbox = App::instance()->getToolBox();
      Rect toolrc;
      int index = 0;

      // Get the chunk of screen where we will draw
      blit(m_overlapped, doublebuffer,
           this->rc->x1 - paintarea.x,
           this->rc->y1 - paintarea.y, 0, 0,
           doublebuffer->w,
           doublebuffer->h);

      for (ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
        Tool* tool = *it;
        if (tool->getGroup() == m_group) {
          ui::Color face;
          int nw;

          if (UIContext::instance()->getSettings()->getCurrentTool() == tool ||
              m_hot_tool == tool) {
            nw = PART_TOOLBUTTON_HOT_NW;
            face = theme->getColor(ThemeColor::ButtonHotFace);
          }
          else {
            nw = PART_TOOLBUTTON_LAST_NW;
            face = theme->getColor(ThemeColor::ButtonNormalFace);
          }

          toolrc = getToolBounds(index++);
          toolrc.offset(-paintarea.x, -paintarea.y);
          theme->draw_bounds_nw(doublebuffer, toolrc, nw, face);

          // Draw the tool icon
          BITMAP* icon = theme->get_toolicon(tool->getId().c_str());
          if (icon) {
            set_alpha_blender();
            draw_trans_sprite(doublebuffer, icon,
                              toolrc.x+toolrc.w/2-icon->w/2,
                              toolrc.y+toolrc.h/2-icon->h/2);
          }
        }
      }

      blit(doublebuffer, ji_screen, 0, 0,
           paintarea.x,
           paintarea.y,
           doublebuffer->w,
           doublebuffer->h);
      destroy_bitmap(doublebuffer);
      return true;
    }

    case kMouseMoveMessage: {
      gfx::Point mousePos = static_cast<MouseMessage*>(msg)->position();
      ToolBox* toolbox = App::instance()->getToolBox();
      Tool* hot_tool = NULL;
      Rect toolrc;
      int index = 0;

      for (ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
        Tool* tool = *it;
        if (tool->getGroup() == m_group) {
          toolrc = getToolBounds(index++);
          if (toolrc.contains(Point(mousePos.x, mousePos.y))) {
            hot_tool = tool;
            break;
          }
        }
      }

      // Hot button changed
      if (m_hot_tool != hot_tool) {
        m_hot_tool = hot_tool;
        invalidate();

        // Show the tooltip for the hot tool
        if (m_hot_tool)
          m_toolbar->openTipWindow(m_group, m_hot_tool);
        else
          m_toolbar->closeTipWindow();

        if (m_hot_tool)
          StatusBar::instance()->showTool(0, m_hot_tool);
      }
      break;
    }

    case kMouseDownMessage:
      if (m_hot_tool) {
        m_toolbar->selectTool(m_hot_tool);
        closeWindow();
      }
      break;

  }
  return Widget::onProcessMessage(msg);
}

void ToolStrip::onPreferredSize(PreferredSizeEvent& ev)
{
  ToolBox* toolbox = App::instance()->getToolBox();
  int c = 0;

  for (ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
    Tool* tool = *it;
    if (tool->getGroup() == m_group) {
      ++c;
    }
  }

  Size iconsize = getToolIconSize(this);
  ev.setPreferredSize(Size(iconsize.w * c, iconsize.h));
}

Rect ToolStrip::getToolBounds(int index)
{
  Size iconsize = getToolIconSize(this);

  return Rect(rc->x1+index*(iconsize.w-1), rc->y1,
              iconsize.w, jrect_h(rc));
}

} // namespace app
