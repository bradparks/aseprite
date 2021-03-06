// Aseprite UI Library
// Copyright (C) 2001-2013  David Capello
//
// This source file is distributed under MIT license,
// please read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ui/clipboard.h"

#include <algorithm>
#include <string>

#ifdef WIN32
#include <allegro.h>
#include <winalleg.h>
#endif

#pragma warning(disable:4996)   // To void MSVC warning about std::copy() with unsafe arguments

static std::string clipboard_text;

static void lowlevel_set_clipboard_text(const char *text)
{
  clipboard_text = text ? text: "";
}

const char* ui::clipboard::get_text()
{
#ifdef WIN32
  if (IsClipboardFormatAvailable(CF_TEXT)) {
    if (OpenClipboard(win_get_window())) {
      HGLOBAL hglobal = GetClipboardData(CF_TEXT);
      if (hglobal != NULL) {
        LPSTR lpstr = static_cast<LPSTR>(GlobalLock(hglobal));
        if (lpstr != NULL) {
          lowlevel_set_clipboard_text(lpstr);
          GlobalUnlock(hglobal);
        }
      }
      CloseClipboard();
    }
  }
#endif

  return clipboard_text.c_str();
}

void ui::clipboard::set_text(const char *text)
{
  lowlevel_set_clipboard_text(text);

#ifdef WIN32
  if (IsClipboardFormatAvailable(CF_TEXT)) {
    if (OpenClipboard(win_get_window())) {
      EmptyClipboard();

      if (!clipboard_text.empty()) {
        int len = clipboard_text.size();

        HGLOBAL hglobal = GlobalAlloc(GMEM_MOVEABLE |
                                      GMEM_ZEROINIT, sizeof(char)*(len+1));

        LPSTR lpstr = static_cast<LPSTR>(GlobalLock(hglobal));
        std::copy(clipboard_text.begin(), clipboard_text.end(), lpstr);
        GlobalUnlock(hglobal);

        SetClipboardData(CF_TEXT, hglobal);
      }
      CloseClipboard();
    }
  }
#endif
}
