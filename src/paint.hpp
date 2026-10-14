#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <cmath>
#include <string>
using namespace Gdiplus;
struct Surface {
  HDC dc = nullptr;
  HBITMAP bmp = nullptr;
  HGDIOBJ old = nullptr;
  void *bits = nullptr;
  int w = 0, h = 0;
  ~Surface() {
    clear();
  }
  void clear() {
    if (dc) {
      SelectObject(dc, old);
      DeleteObject(bmp);
      DeleteDC(dc);
    }
    dc = nullptr;
    bmp = nullptr;
    w = h = 0;
  }
  bool prepare(int x, int y) {
    if (dc && w == x && h == y)
      return true;
    clear();
    w = x;
    h = y;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(nullptr);
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp) {
      clear();
      return false;
    }
    old = SelectObject(dc, bmp);
    return true;
  }
  void present(HWND win, BYTE opacity = 255) {
    RECT r;
    GetWindowRect(win, &r);
    POINT pos{r.left, r.top}, src{};
    SIZE size{w, h};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    UpdateLayeredWindow(win, nullptr, &pos, &size, dc, &src, 0, &blend, ULW_ALPHA);
  }
};
inline Color rgb(unsigned c, int alpha = 255) {
  return Color((BYTE)alpha, (BYTE)(c >> 16), (BYTE)(c >> 8), (BYTE)c);
}
struct Paint {
  Bitmap bitmap;
  Graphics g;
  float scale;
  Color ink, muted, accent, onAccent, track, bg, edge;
  bool dark;
  Paint(Surface &s, float dpi, bool d)
      : bitmap(s.w, s.h, s.w * 4, PixelFormat32bppPARGB, (BYTE *)s.bits), g(&bitmap), scale(dpi),
        dark(d) {
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.Clear(Color(0, 0, 0, 0));
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(dpi, dpi);
    ink = rgb(d ? 0xF4F4F4 : 0x242424);
    muted = rgb(d ? 0xBDBDBD : 0x666666);
    accent = rgb(d ? 0xEBEBEB : 0x242424);
    onAccent = rgb(d ? 0x1B1B1B : 0xFFFFFF);
    track = rgb(d ? 0xBBBBBB : 0x888888, 35);
    bg = rgb(d ? 0x1B1B1B : 0xFAFAFA, d ? 190 : 180);
    edge = rgb(d ? 0xFFFFFF : 0xFFFFFF, d ? 65 : 205);
  }
  void rounded(float x, float y, float w, float h, float r, Color c, bool outline = false) {
    GraphicsPath p;
    if (r <= 0) {
      p.AddRectangle(RectF(x, y, w, h));
    } else {
      float k = r * 2;
      p.AddArc(x, y, k, k, 180, 90);
      p.AddArc(x + w - k, y, k, k, 270, 90);
      p.AddArc(x + w - k, y + h - k, k, k, 0, 90);
      p.AddArc(x, y + h - k, k, k, 90, 90);
      p.CloseFigure();
    }
    if (outline) {
      Pen pen(c, 1);
      g.DrawPath(&pen, &p);
    } else {
      SolidBrush b(c);
      g.FillPath(&b, &p);
    }
  }
  void circle(float x, float y, float r, Color c, bool outline = false, float width = 1.5f) {
    if (outline) {
      Pen p(c, width);
      g.DrawEllipse(&p, x - r, y - r, r * 2, r * 2);
    } else {
      SolidBrush b(c);
      g.FillEllipse(&b, x - r, y - r, r * 2, r * 2);
    }
  }
  void line(float x, float y, float xx, float yy, Color c, float width = 1.7f) {
    Pen p(c, width);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    g.DrawLine(&p, x, y, xx, yy);
  }
  void text(const std::wstring &s, float x, float y, float w, float h, float size, Color c,
            bool mono = false, int align = 1) {
    Font font(mono ? L"Consolas" : L"Microsoft YaHei UI", size, FontStyleRegular, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(align == 1   ? StringAlignmentCenter
                     : align == 2 ? StringAlignmentFar
                                  : StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    g.DrawString(s.c_str(), (int)s.size(), &font, RectF(x, y, w, h), &fmt, &b);
  }
  void reminder(float x, float y, float fullscreen, Color c) {
    // A miniature reminder scene: the same countdown grows from the corner to the centre.
    rounded(x - 12, y - 8, 24, 16, 3, c, true);
    float cx = x + 6 * (1 - fullscreen);
    float cy = y + 3 * (1 - fullscreen);
    float radius = 2.5f + 3.f * fullscreen;
    Pen pen(c, 1.4f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawArc(&pen, cx - radius, cy - radius, radius * 2, radius * 2, -90.f, 285.f);
    line(cx, cy - radius * .5f, cx, cy, c, 1.1f);
    line(cx, cy, cx + radius * .35f, cy + radius * .2f, c, 1.1f);
  }
  void fittedTime(const std::wstring &s, float x, float y, float w, float h, float size, Color c,
                  float emphasis = 1) {
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    // Measure with the same GDI+ layout and padding that DrawString uses.
    // An estimated character width clips digits on some DPI/font combinations.
    Font probe(L"Consolas", size, FontStyleRegular, UnitPixel);
    RectF bounds;
    g.MeasureString(s.c_str(), (int)s.size(), &probe, PointF(0, 0), &fmt, &bounds);
    if (bounds.Width > (w - 2) / 1.04f) size *= (w - 2) / (1.04f * bounds.Width);
    Font font(L"Consolas", size * emphasis, FontStyleRegular, UnitPixel);
    SolidBrush brush(c);
    g.DrawString(s.c_str(), (int)s.size(), &font, RectF(x, y, w, h), &fmt, &brush);
  }
  void icon(int id, float x, float y, Color c) {
    switch (id) {
    case 1:
      circle(x, y, 7, c, true);
      circle(x, y, 2.5f, c, true);
      for (int i = 0; i < 8; i++) {
        float a = i * 3.141593f / 4;
        line(x + cosf(a) * 7, y + sinf(a) * 7, x + cosf(a) * 9, y + sinf(a) * 9, c);
      }
      break;
    case 2:
      line(x - 4, y - 7, x - 4, y + 7, c, 2.4f);
      line(x + 4, y - 7, x + 4, y + 7, c, 2.4f);
      break;
    case 3:
      line(x - 4, y - 7, x + 7, y, c);
      line(x + 7, y, x - 4, y + 7, c);
      line(x - 4, y + 7, x - 4, y - 7, c);
      break;
    case 4:
      circle(x, y, 13, c, true);
      line(x - 6, y, x - 1, y + 5, c, 1.9f);
      line(x - 1, y + 5, x + 7, y - 5, c, 1.9f);
      break;
    case 5:
      line(x - 9, y, x - 5, y, c);
      line(x - 5, y, x - 2, y - 6, c);
      line(x - 2, y - 6, x + 3, y + 6, c);
      line(x + 3, y + 6, x + 6, y, c);
      line(x + 6, y, x + 9, y, c);
      break;
    case 6:
      line(x - 8, y - 3, x - 4, y - 3, c);
      line(x - 4, y - 3, x + 1, y - 7, c);
      line(x + 1, y - 7, x + 1, y + 7, c);
      line(x + 1, y + 7, x - 4, y + 3, c);
      line(x - 4, y + 3, x - 8, y + 3, c);
      line(x - 8, y + 3, x - 8, y - 3, c);
      {
        Pen p(c, 1.6f);
        g.DrawArc(&p, x - 3, y - 7, 14.f, 14.f, -55.f, 110.f);
      }
      break;
    case 7:
      rounded(x - 10, y - 7, 20, 14, 3, c, true);
      line(x - 4, y, x - 1, y + 3, c);
      line(x - 1, y + 3, x + 4, y - 3, c);
      break;
    case 8: {
      Pen p(c, 1.7f);
      g.DrawArc(&p, x - 8, y - 8, 16.f, 16.f, 15.f, 280.f);
      g.DrawArc(&p, x - 1, y - 10, 12.f, 13.f, 80.f, 190.f);
    } break;
    case 9:
      line(x - 4, y - 4, x + 4, y + 4, c);
      line(x + 4, y - 4, x - 4, y + 4, c);
      break;
    case 10: {
      Pen pen(c, 1.7f);
      g.DrawArc(&pen, x - 7, y - 7, 14.f, 14.f, 45.f, 315.f);
      line(x + 3, y - 4, x + 7, y, c);
      line(x + 7, y, x + 11, y - 4, c);
    } break;
    case 11:
      line(x - 3, y + 5, x - 3, y - 7, c);
      line(x - 3, y - 7, x + 7, y - 9, c);
      line(x + 7, y - 9, x + 7, y + 3, c);
      circle(x - 6, y + 5, 3, c);
      circle(x + 4, y + 3, 3, c);
      break;
    case 12:
      rounded(x - 8, y - 6, 16, 12, 2, c, true);
      line(x - 8, y - 2, x + 8, y - 2, c, 1.2f);
      break;
    case 13:
      for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
        line(x + sx * 3, y + sy * 7, x + sx * 8, y + sy * 7, c);
        line(x + sx * 8, y + sy * 7, x + sx * 8, y + sy * 2, c);
      }
      break;
    case 14: {
      Pen pen(c, 1.5f);
      pen.SetStartCap(LineCapRound);
      pen.SetEndCap(LineCapRound);
      g.DrawArc(&pen, x - 5, y - 7, 10.f, 10.f, 180.f, 180.f);
      line(x - 5, y - 2, x - 5, y + 2, c, 1.5f);
      line(x + 5, y - 2, x + 5, y + 2, c, 1.5f);
      line(x - 5, y + 2, x - 7, y + 5, c, 1.5f);
      line(x + 5, y + 2, x + 7, y + 5, c, 1.5f);
      line(x - 7, y + 5, x + 7, y + 5, c, 1.5f);
      g.DrawArc(&pen, x - 2, y + 5, 4.f, 4.f, 20.f, 140.f);
      line(x, y - 9, x, y - 7, c, 1.5f);
    } break;
    }
  }
};
