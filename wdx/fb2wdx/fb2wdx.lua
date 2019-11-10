-- fb2wdx.lua (cross-platform)
-- 2019.11.10
-- Для автодетекта как UTF-8 without BOM --

local r, zip = pcall(require, 'zip')
if r == false then
  -- Fix LuaZip loading (before r8740, DC <= 0.9.1), will wait 0.9.3
  local pt, pe
  if SysUtils.PathDelim == '/' then
    pt = '/.*/'
    pe = '?.so;'
  else
    pt = '.*\\'
    pe = '?.dll;'
  end
  local pc = debug.getinfo(1).source
  if string.sub(pc, 1, 1) == '@' then pc = string.sub(pc, 2, -1) end
  local i, j = string.find(pc, pt)
  if i == nil then return nil end
  pc = string.sub(pc, i, j)
  package.cpath =  pc .. pe .. package.cpath
  r, zip = pcall(require, 'zip')
end

local fields = {
 {"Author(s)",                8},
 {"Author last name",         8},
 {"Author first name",        8},
 {"Author middle name",       8},
 {"Author ID",                8},
 {"Book title",               8},
 {"Translator(s)",            8},
 {"Genres",                   8},
 {"Annotation",               8},
 {"Keywords",                 8},
 {"Sequence",                 8},
 {"Sequence number",          1},
 {"Language",                 8},
 {"Original language",        8},
 {"Date",                     1},
 {"Cover page",               6},
 {"File: date",               8},
 {"File: version",            3},
 {"File: ID",                 8},
 {"File: encoding",           8},
 {"Publish: book name",       8},
 {"Publish: publisher",       8},
 {"Publish: city",            8},
 {"Publish: year",            1},
 {"Publish: ISBN",            8},
 {"Publish: sequence",        8},
 {"Publish: sequence number", 1},
 {"Custom info",              6}
}
local bd
local filename = ''

function ContentGetSupportedField(FieldIndex)
  if fields[FieldIndex + 1] ~= nil then
    return fields[FieldIndex + 1][1], "", fields[FieldIndex + 1][2]
  end
  return "", "", 0
end

function ContentGetDetectString()
  return '(EXT="FB2")|(EXT="FBD")|(EXT="FBZ")|(EXT="ZIP")'
end

function ContentGetValue(FileName, FieldIndex, UnitIndex, flags)
  if FieldIndex > 27 then return nil end
  local e
  if filename ~= FileName then
    e = string.lower(string.sub(FileName, string.len(FileName) - 3, -1))
    if (e ~= '.fb2') and (e ~= '.fbd') and (e ~= '.fbz') and (e ~= '.zip') then
      return nil
    end
    bd = GetFileDescr(FileName, e)
    filename = FileName
  end
  if bd == nil then return nil end
  local pd
  if FieldIndex == 4 then
    return GetTiAutor(bd, 'id')
  elseif FieldIndex == 7 then
    pd = GetTagData(bd, 'title-info')
    local tp, nr1, nr2
    local nr3 = 1
    local gs = ''
    while true do
      if (string.find(bd, '<genre match="', nr3, true) == nil) then
        nr1, nr2 = string.find(bd, '<genre>', nr3, true)
      else
        nr1, nr2 = string.find(bd, '<genre match="%d+">', nr3, false)
      end
      if nr1 == nil then break end
      nr1 = string.find(bd, '</genre>', nr2, true)
      tp = string.sub(bd, nr2 + 1, nr1 - 1)
      gs = gs .. ', ' .. tp
      nr3 = nr1
    end
    if string.len(gs) > 2 then return string.sub(gs, 3, -1) end
    return nil
  elseif FieldIndex == 11 then
    pd = GetTagData(bd, 'title-info')
    pd = GetTagAttr(pd, 'sequence')
    if pd == nil then return nil end
    pd = string.match(pd, 'number="(%d+)"')
    if pd == nil then return nil else return tonumber(pd) end
  elseif FieldIndex == 12 then
    pd = GetTagData(bd, 'title-info')
    pd = GetTagData(pd, 'lang')
    if pd == nil then return nil else return string.lower(pd) end
  elseif FieldIndex == 13 then
    pd = GetTagData(bd, 'title-info')
    pd = GetTagData(pd, 'src-lang')
    if pd == nil then return nil else return string.lower(pd) end
  elseif FieldIndex == 14 then
    pd = GetTagData(bd, 'title-info')
    local d = GetTagAttr(pd, 'date')
    if d ~= nil then
      pd = string.match(d, 'value="(%d+)')
    else
      pd = GetTagData(pd, 'date')
      if pd == nil then return nil end
      pd = string.match(pd, '^%d+')
    end
    if pd == nil then return nil else return tonumber(pd) end
  elseif FieldIndex == 15 then
    pd = GetTagData(bd, 'title-info')
    pd = GetTagData(pd, 'coverpage')
    if pd == nil then return nil end
    if (string.find(pd, ':href="#', 1, true) == nil) then
      return false
    else
      return true
    end
  elseif FieldIndex == 16 then
    pd = GetTagData(bd, 'document-info')
    local d = GetTagAttr(pd, 'date')
    if d ~= nil then
      pd = string.match(d, 'value="([^"]+)"')
    else
      pd = GetTagData(pd, 'date')
      if pd == nil then return nil end
      pd = string.match(pd, '%d%d%d%d')
    end
    if pd == nil then return nil end
    -- ft_date don't supported
    -- local dt = {}
    local nr1 = string.len(pd)
    if nr1 == 4 then
      -- ft_date don't supported
      -- dt.year = tonumber(pd)
      -- dt.month, dt.day = 1, 1
      return pd .. '-01-01'
    elseif nr1 == 10 then
      -- ft_date don't supported
      -- dt.year, dt.month, dt.day = string.match(pd, '(%d*)%-(%d*)%-(%d*)')
      -- for k, v in pairs(dt) do dt[k] = tonumber(v) end
      return pd
    else
      return nil
    end
    -- ft_date don't supported
    -- dt.hour, dt.min, dt.sec = 0, 0, 1
    -- return os.time(dt)
  elseif FieldIndex == 17 then
    pd = GetTagData(bd, 'document-info')
    pd = GetTagData(pd, 'version')
    if pd == nil then return nil else return tonumber(pd) end
  elseif FieldIndex == 18 then
    pd = GetTagData(bd, 'document-info')
    return GetTagData(pd, 'id')
  elseif FieldIndex == 23 then
    pd = GetTagData(bd, 'publish-info')
    if pd == nil then return nil end
    pd = GetTagData(pd, 'year')
    if pd == nil then return nil else return tonumber(pd) end
  elseif FieldIndex == 24 then
    pd = GetTagData(bd, 'publish-info')
    if pd == nil then return nil end
    return GetTagData(pd, 'isbn')
  elseif FieldIndex == 26 then
    pd = GetTagData(bd, 'publish-info')
    if pd == nil then return nil end
    pd = GetTagAttr(pd, 'sequence')
    if pd == nil then return nil end
    pd = string.match(pd, 'number="(%d+)"')
    if pd == nil then return nil else return tonumber(pd) end
  end
  local isUTF8 = false
  local nr1, nr2 = string.find(bd, 'encoding="', 1, true)
  if nr1 == nil then return nil end
  nr1 = string.find(bd, '"', nr2 + 1, true)
  if nr1 == nil then return nil end
  local enc = string.lower(string.sub(bd, nr2 + 1, nr1 - 1))
  if enc == 'utf-8' then isUTF8 = true end
  if FieldIndex == 0 then
    pd = GetFullNames(bd, 'author')
  elseif FieldIndex == 1 then
    pd = GetTiAutor(bd, 'last%-name')
  elseif FieldIndex == 2 then
    pd = GetTiAutor(bd, 'first%-name')
  elseif FieldIndex == 3 then
    pd = GetTiAutor(bd, 'middle%-name')
  elseif FieldIndex == 5 then
    pd = GetTagData(bd, 'title-info')
    pd = GetTagData(pd, 'book-title')
  elseif FieldIndex == 6 then
    pd = GetFullNames(bd, 'translator')
  elseif FieldIndex == 8 then
    pd = GetTagData(bd, 'title-info')
    pd = GetTagData(pd, 'annotation')
  elseif FieldIndex == 9 then
    pd = GetTagData(bd, 'title-info')
    pd = GetTagData(pd, 'keywords')
  elseif FieldIndex == 10 then
    pd = GetTagData(bd, 'title-info')
    pd = GetTagAttr(pd, 'sequence')
    if pd == nil then return nil end
    pd = string.match(pd, 'name="([^"]+)"')
  elseif FieldIndex == 19 then
    return enc
  elseif FieldIndex == 20 then
    pd = GetTagData(bd, 'publish-info')
    if pd == nil then return nil end
    pd = GetTagData(pd, 'book-name')
  elseif FieldIndex == 21 then
    pd = GetTagData(bd, 'publish-info')
    if pd == nil then return nil end
    pd = GetTagData(pd, 'publisher')
  elseif FieldIndex == 22 then
    pd = GetTagData(bd, 'publish-info')
    if pd == nil then return nil end
    pd = GetTagData(pd, 'city')
  elseif FieldIndex == 25 then
    pd = GetTagData(bd, 'publish-info')
    if pd == nil then return nil end
    pd = GetTagAttr(pd, 'sequence')
    if pd == nil then return nil end
    pd = string.match(pd, 'name="([^"]+)"')
  elseif FieldIndex == 27 then
    nr1 = string.find(bd, '<custom-info', 1, true)
    if nr1 == nil then return false end
    nr2 = string.find(bd, '>', nr1, true)
    nr1 = string.find(bd, '</custom-info>', nr2, true)
    if nr1 == nil then return false end
    pd = string.sub(bd, nr2 + 1, nr1 - 1)
    if string.len(pd) > 0 then return true end
    return false
  end
  if pd ~= nil then
    if isUTF8 == false then pd = EncodeToUTF8(pd, enc) end
    if FieldIndex == 8 then return ClearString(pd, true) end
    return ClearString(pd, false)
  end
  return nil
end

function GetFileDescr(fn, fe)
  local h, fd = SysUtils.FindFirst(fn)
  if h == nil then return nil end
  SysUtils.FindClose(h)
  if fd.Attr == -1 then return nil end
  if (math.floor(fd.Attr / 0x00000010) % 2 ~= 0) then return nil end
  local b = 8192
  local o, e, c
  if (fe == '.zip') or (fe == '.fbz') then
    if r == false then return nil end
    local zf = zip.open(fn)
    if not zf then
      zf = zip.open(LazUtf8.ConvertEncoding(fn, 'utf8', 'default'))
      if not zf then return nil end
    end
    for fz in zf:files() do
      e = string.lower(string.sub(fz.filename, -4))
      if (e == '.fb2') or (e == '.fbd') then
        if fz.uncompressed_size < b then b = fz.uncompressed_size end
        local f = zf:open(fz.filename)
        if not f then break end
        c = f:read(b)
        if c == nil then break end
        o = true
        if (string.find(c, '</description>', 1, true) == nil) then
          o = false
          local t
          while true do
            t = f:read(1024)
            if t == nil then break end
            c = c .. t
            if (string.find(c, '</description>', b - 14, true) ~= nil) then
              o = true
              break
            end
          end
        end
        f:close()
        break
      end
    end
    zf:close()
  else
    if fd.Size < b then b = fd.Size end
    local f = io.open(fn, 'r')
    if not f then return nil end
    c = f:read(b)
    if c == nil then return nil end
    o = true
    if (string.find(c, '</description>', 1, true) == nil) then
      o = false
      local t
      while true do
        t = f:read(1024)
        if t == nil then break end
        c = c .. t
        if (string.find(c, '</description>', b - 14, true) ~= nil) then
          o = true
          break
        end
      end
    end
    f:close()
  end
  if o == true then return c end
  return nil
end

function GetTagData(s, t)
  local n1, n2 = string.find(s, '<' .. t .. '>', 1, true)
  if n1 == nil then return nil end
  n1 = string.find(s, '</' .. t .. '>', n2, true)
  s = string.sub(s, n2 + 1, n1 - 1)
  if string.len(s) > 0 then return s end
  return nil
end

function GetTagAttr(s, t)
  local n1 = string.find(s, '<' .. t, 1, true)
  if n1 == nil then return nil end
  local n2 = string.find(s, '>', n1, true)
  s = string.sub(s, n1, n2)
  n1 = string.len('<' .. t .. '>')
  if string.len(s) > n1 then return s end
  return nil
end

function GetFullNames(s, t)
  s = GetTagData(s, 'title-info')
  local n1, n2, tp, an
  local ns = ''
  local n3 = 1
  while true do
    n1 = string.find(s, '<' .. t .. '>', n3, true)
    if n1 == nil then break end
    n2 = string.find(s, '</' .. t .. '>', n1, true)
    tp = string.sub(s, n1, n2)
    ns = ns .. ', '
    an = string.match(tp, '<first%-name>([^<]+)</first%-name>')
    if an ~= nil then ns = ns .. ' ' .. an end
    an = string.match(tp, '<middle%-name>([^<]+)</middle%-name>')
    if an ~= nil then ns = ns .. ' ' .. an end
    an = string.match(tp, '<last%-name>([^<]+)</last%-name>')
    if an ~= nil then ns = ns .. ' ' .. an end
    n3 = n2
  end
  ns = string.gsub(ns, '  +', ' ')
  if string.len(ns) > 2 then return string.sub(ns, 3, -1) end
  return nil
end

function GetTiAutor(s, n)
  s = GetTagData(s, 'title-info')
  s = GetTagData(s, 'author')
  if s == nil then return nil end
  s = string.match(s, '<' .. n .. '>([^<]+)</' .. n .. '>')
  if s == nil then return nil end
  return s
end

function ClearString(s, a)
  local r = {
   ['&lt;'] = '<',
   ['&gt;'] = '>',
   ['&ndash;'] = '-',
   ['&mdash;'] = '-',
   ['&nbsp;'] = ' ',
   ['&apos;'] = "'",
   ['&quot;'] = '"'
  }
  if a then
    s = string.gsub(s, '[\r\n]+', '')
    s = string.gsub(s, '</[pv]>', '\n')
    s = string.gsub(s, '</subtitle>', '\n')
    s = string.gsub(s, '<empty-line', '\n<')
    s = string.gsub(s, '<[^>]+>', '')
    s = string.gsub(s, '\t+', ' ')
    s = string.gsub(s, ' +\n', '\n')
    s = string.gsub(s, '\n +', '\n')
    -- s = string.gsub(s, '\194\160', ' ')
    s = string.gsub(s, '  +', ' ')
  end
  s = string.gsub(s, "&%a+;", function(e)
      return r[e] or e
    end)
  s = string.gsub(s, "&#%d+;", function(e)
      return EntitiesToUTF8(tonumber(string.sub(e, 3, -2)))
    end)
  s = string.gsub(s, "&#x%x+;", function(e)
      return EntitiesToUTF8(tonumber(string.sub(e, 4, -2), 16))
    end)
  s = string.gsub(s, '&amp;', '&')
  -- s = string.gsub(s, '\226\128\146', '-')
  -- s = string.gsub(s, '\226\128\147', '-')
  -- s = string.gsub(s, '\226\128\148', '-')
  -- s = string.gsub(s, '\226\128\149', '-')
  -- s = string.gsub(s, '\226\128\166', '...')
  s = string.gsub(s, '^%s+', '')
  s = string.gsub(s, '%s+$', '')
  return s
end

function EntitiesToUTF8(dec)
  -- https://stackoverflow.com/a/26052539
  local bytemarkers = {{0x7FF, 192}, {0xFFFF, 224}, {0x1FFFFF, 240}}
  if dec < 128 then return string.char(dec) end
  local cbs = {}
  for bytes, vals in ipairs(bytemarkers) do
    if dec <= vals[1] then
      for b = bytes + 1, 2, -1 do
        local mod = dec % 64
        dec = (dec - mod) / 64
        cbs[b] = string.char(128 + mod)
      end
      cbs[1] = string.char(vals[2] + dec)
      break
    end
  end
  return table.concat(cbs)
end

function EncodeToUTF8(s, e)
  local l = {
   ['windows-1250'] = 'cp1250',
   ['windows-1251'] = 'cp1251',
   ['windows-1252'] = 'cp1252',
   ['windows-1253'] = 'cp1253',
   ['windows-1254'] = 'cp1254',
   ['windows-1255'] = 'cp1255',
   ['windows-1256'] = 'cp1256',
   ['windows-1257'] = 'cp1257',
   ['windows-1258'] = 'cp1258',
   ['iso-8859-1'] = 'iso88591',
   ['iso-8859-2'] = 'iso88592',
   ['iso-8859-15'] = 'iso885915'
  }
  if l[e] ~= nil then return LazUtf8.ConvertEncoding(s, l[e], 'utf8') end
  return s
end
