// Aseprite Gfx Library
// Copyright (C) 2001-2013 David Capello
//
// This source file is distributed under MIT license,
// please read LICENSE.txt for more information.

#ifndef GFX_SIZE_H_INCLUDED
#define GFX_SIZE_H_INCLUDED

#include <algorithm>

namespace gfx {

template<typename T>
class PointT;

// A 2D size.
template<typename T>
class SizeT
{
public:
  T w, h;

  SizeT() : w(0), h(0) {
  }

  SizeT(const T& w, const T& h) : w(w), h(h) {
  }

  SizeT(const SizeT& size) : w(size.w), h(size.h) {
  }

  template<typename T2>
  explicit SizeT(const SizeT<T2>& size) : w(static_cast<T>(size.w)),
                                          h(static_cast<T>(size.h)) {
  }

  explicit SizeT(const PointT<T>& point) : w(point.x), h(point.y)  {
  }

  SizeT createUnion(const SizeT& sz) const {
    return Size(std::max(w, sz.w),
                std::max(h, sz.h));
  }

  SizeT createIntersect(const SizeT& sz) const {
    return SizeT(std::min(w, sz.w),
                 std::min(h, sz.h));
  }

  const SizeT& operator=(const SizeT& sz) {
    w = sz.w;
    h = sz.h;
    return *this;
  }

  const SizeT& operator+=(const SizeT& sz) {
    w += sz.w;
    h += sz.h;
    return *this;
  }

  const SizeT& operator-=(const SizeT& sz) {
    w -= sz.w;
    h -= sz.h;
    return *this;
  }

  const SizeT& operator+=(const T& value) {
    w += value;
    h += value;
    return *this;
  }

  const SizeT& operator-=(const T& value) {
    w -= value;
    h -= value;
    return *this;
  }

  const SizeT& operator*=(const T& value) {
    w *= value;
    h *= value;
    return *this;
  }

  const SizeT& operator/=(const T& value) {
    w /= value;
    h /= value;
    return *this;
  }

  SizeT operator+(const SizeT& sz) const {
    return SizeT(w+sz.w, h+sz.h);
  }

  SizeT operator-(const SizeT& sz) const {
    return SizeT(w-sz.w, h-sz.h);
  }

  SizeT operator+(const T& value) const {
    return SizeT(w+value, h+value);
  }

  SizeT operator-(const T& value) const {
    return SizeT(w-value, h-value);
  }

  SizeT operator*(const T& value) const {
    return SizeT(w*value, h*value);
  }

  SizeT operator/(const T& value) const {
    return SizeT(w/value, h/value);
  }

  SizeT operator-() const {
    return SizeT(-w, -h);
  }

  bool operator==(const SizeT& sz) const {
    return w == sz.w && h == sz.h;
  }

  bool operator!=(const SizeT& sz) const {
    return w != sz.w || h != sz.h;
  }

};

typedef SizeT<int> Size;

} // namespace gfx

#endif
