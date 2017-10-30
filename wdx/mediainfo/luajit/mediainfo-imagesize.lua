-- mediainfo-imagesize.lua
-- WDX, for columns set / search
-- 2017.10.05
--
-- Script will return image size (pixels).
--
-- LuaJIT, "MediaInfo.dll" or "libmediainfo.so.0" is used.
--
-- Install LuaJIT:
--   - on Windows: replace lua5.1.dll on lua5.1.dll from LuaJIT Project;
--       download DDL here https://doublecmd.sourceforge.io/forum/viewtopic.php?f=8&t=3615
--   - on Linux:
--       1) install or compile libluajit-5.1 for your OS;
--       2) close DC, open config file doublecmd.xml and change option <PathToLibrary>;
--       Example on Ubuntu:
--         1) in terminal:
--           sudo apt-get install libluajit-5.1-2
--         2) edit doublecmd.xml:
--           <Lua>
--             <PathToLibrary>libluajit-5.1.so.2</PathToLibrary>
--           </Lua>

--====================================================================
local ffi = require("ffi")
local mediaInfo = require("ffi-mediaInfo");
--====================================================================

local mi = nil

local par_name = {
  "Image;%Width%",
  "Image;%Height%"
}

local res = {}
local filename = ""

function ContentSetDefaultParams(IniFileName, PlugApiVerHi, PlugApiVerLow)
  -- create MediaInfo Instance
  mi = mediaInfo.MediaInfoA_New();
end

function ContentPluginUnloading()
  -- delete MediaInto instance
  mediaInfo.MediaInfoA_Delete(mi);
end

function ContentGetSupportedField(Index)
  if (Index == 0) then
    return "Width","", 2; -- FieldName,Units,ft_numeric_64
  elseif (Index == 1) then
    return "Height","", 2;
  end
  return "","", 0;
end

function ContentGetDetectString()
  return '(EXT="BMP") | (EXT="GIF") | (EXT="JFIF") | (EXT="JP2") | (EXT="JPE") | (EXT="JPEG") | (EXT="JPG") | (EXT="PNG") | (EXT="PSD") | (EXT="TGA") | (EXT="TIF") | (EXT="TIFF")';
end

function ContentGetValue(FileName, FieldIndex, UnitIndex, flags)
  if (string.find(FileName, "/", 1, true) == nil) then
    -- Win
    if (string.sub(FileName, -3) == "\\..") or (string.sub(FileName, -2) == "\\.") then
      return nil;
    end
  else
    -- Linux
    if (string.sub(FileName, -3) == "/..") or (string.sub(FileName, -2) == "/.") then
      return nil;
    end
  end
  if (FieldIndex > 1 ) then
    return nil;
  end
  if (filename ~= FileName) then
    -- open target File
    mediaInfo.MediaInfoA_Open (mi, FileName);
    for i=1, 2 do
      -- set option
      mediaInfo.MediaInfoA_Option(mi, "Inform", par_name[i]);
      -- get propety
      res[i] = ffi.string(mediaInfo.MediaInfoA_Inform(mi, 0));
    end
    -- close
    mediaInfo.MediaInfoA_Close (mi);
    filename = FileName;
  end
  result = res[FieldIndex+1];
  return string.gsub(result, "[\r\n]+", "");
end
