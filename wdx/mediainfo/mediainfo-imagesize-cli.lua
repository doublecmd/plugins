-- mediainfo-imagesize.lua
-- 2019.04.25
--
-- Get image size (width and height, pixels).
-- Normal Lua, not LuaJIT. CLI version of MediaInfo.
--
-- Fields: "Width","Height" and "Width x Height"
-- Field "Width x Height" for rename or File Properties dialog (tab "Plugins", Linux only).

-- FieldName, Units, FieldType, Context;Param
local fields = {
 {"Width",          "", 2, "Image;%Width%"},
 {"Height",         "", 2, "Image;%Height%"},
 {"Width x Height", "", 8, "Image;%Width%x%Height%"}
}

function ContentGetSupportedField(FieldIndex)
  if fields[FieldIndex + 1] ~= nil then
    return fields[FieldIndex + 1][1], fields[FieldIndex + 1][2], fields[FieldIndex + 1][3]
  end
  return "", "", 0
end

function ContentGetDetectString()
  return '(EXT="BMP")|(EXT="GIF")|(EXT="JFIF")|(EXT="JP2")|(EXT="JPE")|(EXT="JPEG")|(EXT="JPG")|(EXT="PNG")|(EXT="PSD")|(EXT="TGA")|(EXT="TIF")|(EXT="TIFF")'
end

function ContentGetValue(FileName, FieldIndex, UnitIndex, flags)
  if FieldIndex > 2 then return nil end
  local t = string.sub(FileName, string.len(FileName) - 3, -1)
  if (t == "/..") or (t == "\\..") then return nil end
  t = string.sub(t, 2, -1)
  if (t == "/.") or (t == "\\.") then return nil end
  if flags == 1 then
    if FieldIndex ~= 2 then return nil end
  end
  local h = io.popen('mediainfo --Inform="' .. fields[FieldIndex + 1][4] .. '" "' .. FileName .. '"')
  if not h then return nil end
  local res = h:read("*a")
  h:close()
  return string.gsub(res, "[\r\n]+", "")
end
