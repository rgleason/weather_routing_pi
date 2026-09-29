/**********************************************************************
zyGrib: meteorological GRIB file viewer
Copyright (C) 2008 - Jacques Zaninetti - http://www.zygrib.org

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
***********************************************************************/
/**
 * \file
 * \implements \ref zuFile.h
 */
#include "zuFile.h"
#include <wx/wxcrt.h>
#include <climits>
#include <algorithm>
#include <memory>

#ifdef __WXMSW__
// The bundled zlib 1.2.3 header omits this declaration. Both Windows SDK
// libraries export it; let zlib open the Unicode path in its own CRT.
extern "C" {
ZEXTERN gzFile ZEXPORT gzopen_w(const wchar_t* path, const char* mode);
}
#endif

// Avoid locale-dependent wx filename conversions on Unix: the public API
// already supplies UTF-8. Windows retains native wide-character filenames.
static FILE* openNative(const wxString& name, const char* utf8, const char* mode) {
#ifdef __WXMSW__
  wchar_t wideMode[16] = {};
  std::size_t i = 0;
  for (; mode[i] && i < 15; ++i) wideMode[i] = static_cast<unsigned char>(mode[i]);
  if (mode[i]) return nullptr;
  return _wfopen(name.wc_str(), wideMode);
#else
  (void)name;
  return fopen(utf8, mode);
#endif
}

int zu_can_read_file(const char* fname) {
  ZUFILE* file = zu_open(fname, "rb");
  if (!file) return 0;
  zu_close(file);
  return 1;
}

ZUFILE* zu_open(const char* fname, const char* mode, int type) {
  if (!fname) return nullptr;
  return zu_open_wx(wxString::FromUTF8(fname), mode, type);
}

ZUFILE* zu_open_wx(const wxString& fname, const char* mode, int type) {
  const wxScopedCharBuffer utf8 = fname.utf8_str();
  if (fname.empty() || !utf8.data() || !*utf8.data() || !mode) return nullptr;
  // Zero initialization makes every failed-open path safe to close.
  ZUFILE* f = static_cast<ZUFILE*>(calloc(1, sizeof(ZUFILE)));
  if (!f) return nullptr;
  std::unique_ptr<ZUFILE, decltype(&zu_close)> owner(f, &zu_close);
  f->fname = strdup(utf8.data());
  if (!f->fname) return nullptr;
  f->type = type;
  if (type == ZU_COMPRESS_AUTO) {
    const char* ext = strrchr(f->fname, '.');
    char suffix[5] = {};
    if (ext) for (unsigned i = 0; i < 4 && ext[i]; ++i)
      suffix[i] = static_cast<char>(tolower(static_cast<unsigned char>(ext[i])));
    f->type = !strcmp(suffix, ".gz") ? ZU_COMPRESS_GZIP :
              (!strcmp(suffix, ".bz2") || !strcmp(suffix, ".bz")) ? ZU_COMPRESS_BZIP : ZU_COMPRESS_NONE;
  }
  switch (f->type) {
    case ZU_COMPRESS_NONE:
      f->zfile = openNative(fname, f->fname, mode);
      break;
    case ZU_COMPRESS_GZIP:
#ifdef __WXMSW__
      f->zfile = gzopen_w(fname.wc_str(), mode);
#else
      f->zfile = gzopen(f->fname, mode);
#endif
      break;
    case ZU_COMPRESS_BZIP:
      f->faux = openNative(fname, f->fname, mode);
      if (f->faux) {
        int error = BZ_OK;
        f->zfile = BZ2_bzReadOpen(&error, f->faux, 0, 0, nullptr, 0);
        if (error != BZ_OK) { return nullptr; }
      }
      break;
    default: break;
  }
  if (!f->zfile) { return nullptr; }
  f->ok = 1;
  return owner.release();
}

int zu_close(ZUFILE* f) {
  if (!f) return 0;
  if (f->zfile) {
    switch (f->type) {
      case ZU_COMPRESS_NONE: fclose(static_cast<FILE*>(f->zfile)); break;
      case ZU_COMPRESS_GZIP: gzclose(static_cast<gzFile>(f->zfile)); break;
      case ZU_COMPRESS_BZIP: {
        int error;
        BZ2_bzReadClose(&error, static_cast<BZFILE*>(f->zfile));
        break;
      }
    }
  }
  if (f->faux) fclose(f->faux);
  free(f->fname);
  free(f);
  return 0;
}

int zu_read(ZUFILE* f, void* buf, long len) {
  if (!f || !f->zfile || !f->ok || len < 0 || len > INT_MAX ||
      (len && !buf) || f->pos < 0 || len > LONG_MAX - f->pos) return -1;
  if (!len) return 0;
  int count = 0;
  switch (f->type) {
    case ZU_COMPRESS_NONE:
      count = static_cast<int>(fread(buf, 1, len, static_cast<FILE*>(f->zfile)));
      if (ferror(static_cast<FILE*>(f->zfile))) f->ok = 0;
      break;
    case ZU_COMPRESS_GZIP:
      count = gzread(static_cast<gzFile>(f->zfile), buf, static_cast<unsigned>(len));
      if (count < 0) { f->ok = 0; return -1; }
      break;
    case ZU_COMPRESS_BZIP: {
      int error;
      count = BZ2_bzRead(&error, static_cast<BZFILE*>(f->zfile), buf, static_cast<int>(len));
      if (error != BZ_OK && error != BZ_STREAM_END) { f->ok = 0; return -1; }
      break;
    }
    default: return -1;
  }
  f->pos += count;  // errors never move the logical position backwards
  return count;
}

char* zu_gets(ZUFILE* f, char* buf, int len) {
  if (!f || !buf || len < 2 || !f->zfile || !f->ok || f->pos < 0) return nullptr;

  if (f->type == ZU_COMPRESS_NONE || f->type == ZU_COMPRESS_GZIP) {
    char* result = f->type == ZU_COMPRESS_NONE
        ? fgets(buf, len, static_cast<FILE*>(f->zfile))
        : gzgets(static_cast<gzFile>(f->zfile), buf, len);
    if (!result) {
      if ((f->type == ZU_COMPRESS_NONE && ferror(static_cast<FILE*>(f->zfile))) ||
          (f->type == ZU_COMPRESS_GZIP && !gzeof(static_cast<gzFile>(f->zfile))))
        f->ok = 0;
      return nullptr;
    }
    const std::size_t size = strlen(buf);
    if (size > static_cast<std::size_t>(LONG_MAX - f->pos)) {
      f->ok = 0;
      return nullptr;
    }
    f->pos += static_cast<long>(size);
    return result;
  }

  // A bzip stream cannot cheaply unread a block after a newline. Read one
  // byte at a time; polar files are small and each read updates the offset.
  int used = 0;
  while (used < len - 1) {
    char value;
    if (zu_read(f, &value, 1) != 1) break;
    buf[used++] = value;
    if (value == '\n') break;
  }
  if (!used) return nullptr;
  buf[used] = '\0';
  return buf;
}

long zu_tell(ZUFILE* f) { return f ? f->pos : -1; }
long zu_filesize(ZUFILE* f) {
  if (!f || !f->fname) return -1;
  #ifdef __WXMSW__
  FILE* file = openNative(wxString::FromUTF8(f->fname), f->fname, "rb");
#else
  FILE* file = fopen(f->fname, "rb");
#endif
  if (!file) return -1;
  const long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
  fclose(file);
  return size;  // physical size; compressed size is not a decoded-size bound
}

int zu_bzSeekForward(ZUFILE* f, unsigned long bytes) {
  if (!f || f->type != ZU_COMPRESS_BZIP || !f->zfile || !f->ok ||
      f->pos < 0 || bytes > static_cast<unsigned long>(LONG_MAX - f->pos)) return -1;
  char buffer[ZU_BUFREADSIZE];
  while (bytes) {
    const unsigned long request = std::min<unsigned long>(bytes, sizeof(buffer));
    const int count = zu_read(f, buffer, static_cast<long>(request));
    if (count <= 0) return -1;  // truncated/error input must make progress
    bytes -= count;
  }
  return 0;
}

int zu_seek(ZUFILE* f, long offset, int whence) {
  if (!f || !f->zfile || (whence != SEEK_SET && whence != SEEK_CUR)) return -1;
  if (whence == SEEK_CUR) {
    if ((offset > 0 && offset > LONG_MAX - f->pos) || offset < -f->pos) return -1;
    offset += f->pos;
  }
  if (offset < 0) return -1;
  switch (f->type) {
    case ZU_COMPRESS_NONE:
      if (fseek(static_cast<FILE*>(f->zfile), offset, SEEK_SET)) return -1;
      f->pos = offset;
      f->ok = 1;
      return 0;
    case ZU_COMPRESS_GZIP:
      if (gzseek(static_cast<gzFile>(f->zfile), offset, SEEK_SET) < 0) return -1;
      f->pos = offset;
      f->ok = 1;
      return 0;
    case ZU_COMPRESS_BZIP:
      if (offset < f->pos || !f->ok) {
        int error;
        BZ2_bzReadClose(&error, static_cast<BZFILE*>(f->zfile));
        f->zfile = nullptr;
        if (fseek(f->faux, 0, SEEK_SET)) { f->ok = 0; return -1; }
        f->zfile = BZ2_bzReadOpen(&error, f->faux, 0, 0, nullptr, 0);
        if (error != BZ_OK || !f->zfile) { f->ok = 0; return -1; }
        f->pos = 0;
        f->ok = 1;
      }
      return zu_bzSeekForward(f, static_cast<unsigned long>(offset - f->pos));
  }
  return -1;
}
void zu_rewind(ZUFILE* f) { (void)zu_seek(f, 0, SEEK_SET); }
