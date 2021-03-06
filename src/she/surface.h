// SHE library
// Copyright (C) 2012-2013  David Capello
//
// This source file is distributed under MIT license,
// please read LICENSE.txt for more information.

#ifndef SHE_SURFACE_H_INCLUDED
#define SHE_SURFACE_H_INCLUDED

namespace she {

  class LockedSurface;

  class Surface {
  public:
    virtual ~Surface() { }
    virtual void dispose() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual LockedSurface* lock() = 0;
    virtual void* nativeHandle() = 0;
  };

  class NotDisposableSurface : public Surface {
  public:
    virtual ~NotDisposableSurface() { }
  private:
    virtual void dispose() = 0;
  };

} // namespace she

#endif
