-- mediainfo-imagesize.lua
-- WDX, for columns set / search
-- 2017.10.05
--
-- Script will return image size (pixels).
--
-- Normal Lua, not LuaJIT. CLI version of MediaInfo is used.
--
-- Field "Width x Height" for rename or File Properties dialog (tab "Plugins").

function ContentGetSupportedField(Index)
  if (Index == 0) then
    return "Width","", 2; -- FieldName,Units,ft_numeric_64
  elseif (Index == 1) then
    return "Height","", 2;
  elseif (Index == 2) then
    return "Width x Height","", 8; -- FieldName,Units,ft_string
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
  local par
  if (flags == 1) then
    if (FieldIndex == 2) then
      par = "Image;%Width%x%Height%"
    else
      return nil;
    end
  else
    if (FieldIndex == 0) then
      par = "Image;%Width%";
    elseif (FieldIndex == 1) then
      par = "Image;%Height%";
    elseif (FieldIndex == 2) then
      par = "Image;%Width%x%Height%";
    else
      return nil;
    end
  end
  local handle = io.popen('mediainfo --Inform="' .. par .. '" "' .. FileName .. '"');
  local result = handle:read("*a");
  handle:close();
  return string.gsub(result, "[\r\n]+", "");
end
