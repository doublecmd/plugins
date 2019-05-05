-- mediainfo-imagesize.lua
-- 2019.04.25
--
-- Get image size (width and height, pixels).
-- Fields: "Width","Height" and "Width x Height"
--
-- How to use LuaJIT and MediaInfo: see mediainfo-video.txt

--========================================
local ffi = require("ffi")
local mediaInfo = require("ffi-mediaInfo")
--========================================

local mi = nil
local par_name = {
 "Image;%Width%",
 "Image;%Height%"
}
local res = {}
local filename = ""

function ContentSetDefaultParams(IniFileName, PlugApiVerHi, PlugApiVerLow)
  -- Create MediaInfo Instance
  mi = mediaInfo.MediaInfoA_New()
end

function ContentPluginUnloading()
  -- Delete MediaInto instance
  mediaInfo.MediaInfoA_Delete(mi)
end

function ContentGetSupportedField(FieldIndex)
  if FieldIndex == 0 then
    return "Width", "", 2; -- FieldName,Units,ft_numeric_64
  elseif FieldIndex == 1 then
    return "Height", "", 2
  elseif FieldIndex == 2 then
    return "Width x Height", "", 8; -- FieldName,Units,ft_string
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
  if filename ~= FileName then
    -- Open target File
    mediaInfo.MediaInfoA_Open(mi, FileName)
    for i = 1, 2 do
      -- Set option
      mediaInfo.MediaInfoA_Option(mi, "Inform", par_name[i])
      -- Get propety
      res[i] = ffi.string(mediaInfo.MediaInfoA_Inform(mi, 0))
    end
    -- Close
    mediaInfo.MediaInfoA_Close(mi)
    filename = FileName
  end
  if FieldIndex == 2 then
    return res[1] .. "x" .. res[2]
  else
    return res[FieldIndex + 1]
  end
  return nil
end
