#include "config.h"

#if defined(BUILD_GTK) && !defined(MEM_CHECK)

#include "platform/cairo/graphic_cairo.h"

#include <fontconfig/fontconfig.h>
// FcFreeTypeQuery() (used below) is declared here, not in fontconfig.h --
// missing on some fontconfig-dev packagings that don't transitively pull
// this in (confirmed missing on Ubuntu 24.04's libfontconfig-dev).
#include <fontconfig/fcfreetype.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <utility>

using namespace tex;
using namespace std;

// Lazily-initialized, process-lifetime FreeType library handle used as a
// fallback path below (see loadFont()) when Fontconfig can't produce a
// usable pattern for one of our own bundled font files. Never torn down --
// matches Fontconfig's own de-facto "leak for process lifetime" handling of
// its global config elsewhere in this file.
static FT_Library ftLibrary() {
  static FT_Library lib = [] {
    FT_Library l = nullptr;
    FT_Init_FreeType(&l);
    return l;
  }();
  return lib;
}

map<string, string> Font_cairo::_families;
map<string, Cairo::RefPtr<Cairo::FtFontFace>> Font_cairo::_cairoFtFaces;

Font_cairo::Font_cairo(string family, int style, float size)
  : _family(std::move(family)), _style(style), _size((double) size) {}

Font_cairo::Font_cairo(const string& file, float size) : Font_cairo("", PLAIN, size) {
  loadFont(file);
}

void Font_cairo::loadFont(const string& file) {
  auto ffaceEntry = _cairoFtFaces.find(file);
  auto familyEntry = _families.find(file);
  if (ffaceEntry != _cairoFtFaces.end() && familyEntry != _families.end()) {
    // already loaded
    _family = familyEntry->second;
    _fface = ffaceEntry->second;
#ifdef HAVE_LOG
    __log << file << " already loaded, skip\n";
#endif
    return;
  }

  // query font via fontconfig
  const FcChar8* f = (const FcChar8*) file.c_str();

  // get font family from file first
  int count;
  FcChar8* family = NULL;
  FcBlanks* blanks = FcConfigGetBlanks(NULL);
  FcPattern* p = FcFreeTypeQuery(f, 0, blanks, &count);
  // FcFreeTypeQuery can return NULL (font file Fontconfig couldn't parse),
  // and even when it doesn't, FcPatternGetString finding no FC_FAMILY entry
  // leaves `family` untouched (still NULL) rather than erroring loudly --
  // the original code didn't check either return value, so a NULL `family`
  // reached `(const char*) family` below and crashed std::string's
  // assignment operator on strlen(nullptr). Confirmed against this
  // project's own vendored LaTeX symbol fonts (cmmi10.ttf etc.), which
  // Fontconfig apparently doesn't always report a family name for.
  if (p) FcPatternGetString(p, FC_FAMILY, 0, &family);
#ifdef HAVE_LOG
  __dbg("Load font: %s, count: %d\n", file.c_str(), count);
  if (p) FcPatternPrint(p);
#endif

  // load font to fontconfig
  FcBool status = FcConfigAppFontAddFile(NULL, f);
#ifdef HAVE_LOG
  if (!status) __dbg(ANSI_COLOR_RED "Load %s failed\n" ANSI_RESET, file.c_str());
#endif

  _family = family ? (const char*) family : file;
  _families[file] = _family;

  if (p) {
    _fface = Cairo::FtFontFace::create(p);
  } else {
    // Fontconfig couldn't produce a pattern for this file (observed against
    // this project's own vendored LaTeX symbol fonts, e.g. cmmi10.ttf) --
    // rather than leaving _fface null (which later crashes
    // Cairo::Context::set_font_face() in drawText(), since cairomm doesn't
    // guard against a null RefPtr there), load the face directly via
    // FreeType and hand cairomm that instead. The FT_Face is intentionally
    // never released -- it must outlive the Cairo::FtFontFace built from it,
    // and both are cached for the process lifetime via the static maps
    // here, same as the Fontconfig-pattern path above.
    static map<string, FT_Face> ftFaces;
    FT_Face ftFace = nullptr;
    FT_Error err = FT_New_Face(ftLibrary(), file.c_str(), 0, &ftFace);
    if (err == 0 && ftFace) {
      ftFaces[file] = ftFace;
      _fface = Cairo::FtFontFace::create(ftFace, 0);
    } else {
      _fface = Cairo::RefPtr<Cairo::FtFontFace>();
#ifdef HAVE_LOG
      __dbg(ANSI_COLOR_RED "FreeType fallback also failed to load %s\n" ANSI_RESET, file.c_str());
#endif
    }
  }
  _cairoFtFaces[file] = _fface;

  // release
  if (p) FcPatternDestroy(p);
}

string Font_cairo::getFamily() const {
  return _family;
}

Cairo::RefPtr<Cairo::FtFontFace> Font_cairo::getCairoFontFace() const {
  return _fface;
}

int Font_cairo::getStyle() const {
  return _style;
}

float Font_cairo::getSize() const {
  return (float) _size;
}

sptr<Font> Font_cairo::deriveFont(int style) const {
  return sptrOf<Font_cairo>(_family, style, _size);
}

bool Font_cairo::operator==(const Font& ft) const {
  const Font_cairo& f = static_cast<const Font_cairo&>(ft);
  return _size == f._size && _style == f._style && _family == f._family;
}

bool Font_cairo::operator!=(const Font& f) const {
  return !(*this == f);
}

Font* Font::create(const string& file, float size) {
  return new Font_cairo(file, size);
}

sptr<Font> Font::_create(const string& name, int style, float size) {
  return sptrOf<Font_cairo>(name, style, size);
}

/**************************************************************************************************/

Cairo::RefPtr<Cairo::Context> TextLayout_cairo::_img_context;

TextLayout_cairo::TextLayout_cairo(const wstring& src, const sptr<Font_cairo>& f) {
  if (!_img_context) {
    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, 1, 1);
    _img_context = Cairo::Context::create(surface);
  }

  _layout = Pango::Layout::create(_img_context);

  Pango::FontDescription fd;
  fd.set_family(f->getFamily());
  fd.set_absolute_size(f->getSize() * Pango::SCALE);
  fd.set_style(Pango::STYLE_NORMAL);
  fd.set_weight(Pango::WEIGHT_NORMAL);

  switch (f->getStyle()) {
    case BOLD:
      fd.set_weight(Pango::WEIGHT_BOLD);
      break;
    case ITALIC:
      fd.set_style(Pango::STYLE_ITALIC);
      break;
    case BOLDITALIC:
      fd.set_style(Pango::STYLE_ITALIC);
      fd.set_weight(Pango::WEIGHT_BOLD);
      break;
  }

  _layout->set_text(wide2utf8(src));
  _layout->set_font_description(fd);

  _ascent = (float) (_layout->get_baseline() / Pango::SCALE);
}

void TextLayout_cairo::getBounds(Rect& r) {
  int w, h;
  _layout->get_pixel_size(w, h);
  r.x = 0;
  r.y = -_ascent;
  r.w = (float) w;
  r.h = (float) h;
}

void TextLayout_cairo::draw(Graphics2D& g2, float x, float y) {
  // FIXME
  // draw line, I don't know why the layout is shown in wrong position
  // when line was not drawn
  color old = g2.getColor();
  g2.setColor(0x00000000);
  g2.drawLine(x, y, x + 1, y);
  // draw layout
  g2.setColor(old);
  g2.translate(x, y - _ascent);
  auto& g = static_cast<Graphics2D_cairo&>(g2);
  _layout->show_in_cairo_context(g.getCairoContext());
  g2.translate(-x, -y + _ascent);
}

sptr<TextLayout> TextLayout::create(const wstring& src, const sptr<Font>& font) {
  sptr<Font_cairo> f = static_pointer_cast<Font_cairo>(font);
  return sptrOf<TextLayout_cairo>(src, f);
}

/**************************************************************************************************/

Font_cairo Graphics2D_cairo::_default_font("SansSerif", PLAIN, 20.f);

Graphics2D_cairo::Graphics2D_cairo(const Cairo::RefPtr<Cairo::Context>& context)
  : _context(context) {
  _sx = _sy = 1.f;
  setColor(BLACK);
  setStroke(Stroke());
  setFont(&_default_font);
}

const Cairo::RefPtr<Cairo::Context>& Graphics2D_cairo::getCairoContext() const {
  return _context;
}

void Graphics2D_cairo::setColor(color c) {
  _color = c;
  const double a = color_a(c) / 255.;
  const double r = color_r(c) / 255.;
  const double g = color_g(c) / 255.;
  const double b = color_b(c) / 255.;
  _context->set_source_rgba(r, g, b, a);
}

color Graphics2D_cairo::getColor() const {
  return _color;
}

void Graphics2D_cairo::setStroke(const Stroke& s) {
  _stroke = s;
  _context->set_line_width((double) s.lineWidth);

  // convert abstract line cap to platform line cap
  Cairo::LineCap c;
  switch (s.cap) {
    case CAP_BUTT:
      c = Cairo::LINE_CAP_BUTT;
      break;
    case CAP_ROUND:
      c = Cairo::LINE_CAP_ROUND;
      break;
    case CAP_SQUARE:
      c = Cairo::LINE_CAP_SQUARE;
      break;
  }
  _context->set_line_cap(c);

  // convert abstract line join to platform line join
  Cairo::LineJoin j;
  switch (s.join) {
    case JOIN_BEVEL:
      j = Cairo::LINE_JOIN_BEVEL;
      break;
    case JOIN_ROUND:
      j = Cairo::LINE_JOIN_ROUND;
      break;
    case JOIN_MITER:
      j = Cairo::LINE_JOIN_MITER;
      break;
  }
  _context->set_line_join(j);

  _context->set_miter_limit((double) s.miterLimit);
}

const Stroke& Graphics2D_cairo::getStroke() const {
  return _stroke;
}

void Graphics2D_cairo::setStrokeWidth(float w) {
  _stroke.lineWidth = w;
  _context->set_line_width((double) w);
}

const Font* Graphics2D_cairo::getFont() const {
  return _font;
}

void Graphics2D_cairo::setFont(const Font* font) {
  _font = static_cast<const Font_cairo*>(font);
}

void Graphics2D_cairo::translate(float dx, float dy) {
  _context->translate((double) dx, (double) dy);
}

void Graphics2D_cairo::scale(float sx, float sy) {
  _sx *= sx;
  _sy *= sy;
  _context->scale((double) sx, (double) sy);
}

void Graphics2D_cairo::rotate(float angle) {
  _context->rotate(angle);
}

void Graphics2D_cairo::rotate(float angle, float px, float py) {
  _context->translate((double) px, (double) py);
  _context->rotate(angle);
  _context->translate((double) -px, (double) -py);
}

void Graphics2D_cairo::reset() {
  _context->set_identity_matrix();
  _sx = _sy = 1.f;
}

float Graphics2D_cairo::sx() const {
  return _sx;
}

float Graphics2D_cairo::sy() const {
  return _sy;
}

void Graphics2D_cairo::drawChar(wchar_t c, float x, float y) {
  wstring str = {c, L'\0'};
  drawText(str, x, y);
}

void Graphics2D_cairo::drawText(const wstring& t, float x, float y) {
  // getCairoFontFace() can still legitimately be a null RefPtr if both the
  // Fontconfig and FreeType-direct load paths in Font_cairo::loadFont()
  // failed for this font file -- cairomm's set_font_face() doesn't
  // null-check and segfaults, so skip it here and fall back to whatever
  // font face cairo already has selected (its own default toy font).
  auto fface = _font->getCairoFontFace();
  if (fface) _context->set_font_face(fface);
  _context->set_font_size(_font->getSize());
  _context->move_to(x, y);
  _context->show_text(wide2utf8(t));
}

void Graphics2D_cairo::drawLine(float x1, float y1, float x2, float y2) {
  _context->move_to(x1, y1);
  _context->line_to(x2, y2);
  _context->stroke();
}

void Graphics2D_cairo::drawRect(float x, float y, float w, float h) {
  _context->rectangle(x, y, w, h);
  _context->stroke();
}

void Graphics2D_cairo::fillRect(float x, float y, float w, float h) {
  _context->rectangle(x, y, w, h);
  _context->fill();
}

void Graphics2D_cairo::roundRect(float x, float y, float w, float h, float rx, float ry) {
  double r = max(rx, ry);
  double d = G_PI / 180.;
  _context->begin_new_sub_path();
  _context->arc(x + r, y + r, r, 180 * d, 270 * d);
  _context->arc(x + w - r, y + r, r, -90 * d, 0);
  _context->arc(x + w - r, y + h - r, r, 0, 90 * d);
  _context->arc(x + r, y + h - r, r, 90 * d, 180 * d);
  _context->close_path();
}

void Graphics2D_cairo::drawRoundRect(float x, float y, float w, float h, float rx, float ry) {
  roundRect(x, y, w, h, rx, ry);
  _context->stroke();
}

void Graphics2D_cairo::fillRoundRect(float x, float y, float w, float h, float rx, float ry) {
  roundRect(x, y, w, h, rx, ry);
  _context->fill();
}

#endif
