fb2wdx.lua
2020.09.11

Get some FictionBook2 (FB2) information (see "4. Fields" below).
Cross-platform.

1. Requeres

- Double Commander >= 0.9.0;
- Lua library (LuaJIT, Lua 5.1).

NOTE: Also you can use Lua 5.2 or 5.3, but in this case you need DC >= 0.9.2 (or >= r8756) and if you want to use LuaZip module (see below), then you must try to build LuaZip witn Lua 5.2 or 5.3 yourself.

2. List of supported encoding

- UTF-8;
- Windows-125x (Windows-1250 - Windows-1258);
- ISO-8859-1, ISO-8859-2, ISO-8859-15.

3. Reading FB2-files from Zip-archives

For reading FB2-files from Zip-archives (*.fb2.zip, *.zip or *.fbz) requires LuaZip module.
NOTE: Works with first found FB2- or FBD-file in archive.

- Windows:
  zip.dll must be near doublecmd.exe or near fb2wdx.lua:
  LuaZip-win/i386-win32/zip.dll for 32-bit version of DC,
  LuaZip-win/x86_64-win64/zip.dll for 64-bit version of DC
  (builded from https://github.com/mpeterv/luazip with Lua 5.1, thanks for the binary files to Alexander Koblov aka Alexx2000).
- Linux:
  Find LuaZip package, for example: "lua-zip" (Debian/Ubuntu and Debian/Ubuntu-based), "luazip5.1" (Arch Linux, from AUR).
  If not exists then try LuaRocks or try build:
    https://github.com/luaforge/luazip (used in Debian)
    https://github.com/mpeterv/luazip (fork with some fixes/changes)
    https://github.com/msva/luazip (fork too, used in Gentoo?)

4. Fields

NOTE: Field "Annotation" may contain line break(s) (line feed, i.e. LF or \n) and Double Commander will keep them (when using cm_CopyFileDetailsToClip or in tooltips).

(value: s - string, n - number, f - float number, b - boolean)

Book info:
- Author(s)
    s: full name of the author (if more than one then comma separated);
- Author last name
    s: if more than one then first found author;
- Author first name
    s: if more than one then first found author;
- Author middle name
    s: if more than one then first found author;
- Author ID
    s: for libraries (if more than one then first found author);
- Book title
    s: title of book;
- Translator(s)
    s: full name of the translator (if more than one then comma separated);
- Genres
    s: if more than one then comma separated;
- Annotation
    s: abstract, a brief summary;
- Keywords
    s: keywords for search engines;
- Sequence
    s: if part of series: name of series;
- Sequence number
    n: if part of series: number of series;
- Language
    s: language of book (two-letter code);
- Original language
    s: language of original book (two-letter code);
- Date
    n: writing date (year);
- Cover page
    b: exists or not;
FB2 file info:
- File: date
    s: file creation date (from tag, not from file system), "yyyy-mm-dd";
- File: version
    f: file version;
- File: ID
    s: file ID, like "Author ID";
- File: encoding
    s: file encoding;
Original (paper) book info:
- Publish: book name
    s: name of original book;
- Publish: publisher
    s: publisher;
- Publish: city
    s: where published;
- Publish: year
    n: published;
- Publish: ISBN
    s: ISBN;
- Publish: sequence
    s: like "Sequence";
- Publish: sequence number
    n: like "Sequence number";
Extra:
- Custom info
    b: exists or not.
