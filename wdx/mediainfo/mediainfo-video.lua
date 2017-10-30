-- mediainfo-video.lua
-- WDX, for columns set / search
-- 2017.10.05
--
-- Script will return some media info (for video).
--
-- Normal Lua, not LuaJIT. CLI version of MediaInfo is used.
--
-- Fields 
--   "General:" (Duration HH:MM:SS, Overall bit rate, auto Bps/KBps/MBps)
--   "Video:" (Bit rate, auto Bps/KBps/MBps, Width x Height, Frame rate)
--  for File Properties dialog (tab "Plugins").

function ContentGetSupportedField(Index)
  if (Index == 0) then
    return "General: MIME/Content-Type","", 8; -- FieldName,Units,ft_string
  elseif (Index == 1) then
    return "General: Format","", 8;
  elseif (Index == 2) then
    return "General: Duration ms","", 2; -- FieldName,Units,ft_numeric_64
  elseif (Index == 3) then
    return "General: Duration HH:MM:SS","", 8;
  elseif (Index == 4) then
    return "General: Duration HH:MM:SS.MMM","", 8;
  elseif (Index == 5) then
    return "General: Overall bit rate","Bps|KBps|MBps", 3; -- FieldName,Units,ft_numeric_floating
  elseif (Index == 6) then
    return "General: Overall bit rate, auto Bps/KBps/MBps","", 8;
  elseif (Index == 7) then
    return "General: Count of streams","", 2;
  elseif (Index == 8) then
    return "General: Number of audio streams","", 2;
  elseif (Index == 9) then
    return "General: Number of text streams","", 2;
  elseif (Index == 10) then
    return "General: Number of image streams","", 2;
  elseif (Index == 11) then
    return "General: Number of menu streams","", 2;
  elseif (Index == 12) then
    return "Video: Format","", 8;
  elseif (Index == 13) then
    return "Video: Codec ID","", 8;
  elseif (Index == 14) then
    return "Video: Streamsize in bytes","", 2;
  elseif (Index == 15) then
    return "Video: Bit rate","Bps|KBps|MBps", 3;
  elseif (Index == 16) then
    return "Video: Bit rate, auto Bps/KBps/MBps","", 8;
  elseif (Index == 17) then
    return "Video: Width","", 2;
  elseif (Index == 18) then
    return "Video: Height","", 2;
  elseif (Index == 19) then
    return "Video: Width x Height","", 8;
  elseif (Index == 20) then
    return "Video: Display Aspect ratio","", 3;
  elseif (Index == 21) then
    return "Video: DisplayAspectRatio (string)","", 8;
  elseif (Index == 22) then
    return "Video: Pixel Aspect ratio","", 3;
  elseif (Index == 23) then
    return "Video: Frame rate mode","", 8;
  elseif (Index == 24) then
    return "Video: Frame rate","", 3;
  elseif (Index == 25) then
    return "Video: Number of frames","", 2;
  elseif (Index == 26) then
    return "Video: Rotation","", 2;
  elseif (Index == 27) then
    return "General:","", 8;
  elseif (Index == 28) then
    return "Video:","", 8;
  end
  return "","", 0;
end

function ContentGetDetectString()
  return '(EXT="3G2") | (EXT="3GP") | (EXT="ASF") | (EXT="AVI") | (EXT="DIVX") | (EXT="FLV") | (EXT="M2T") | (EXT="M2TS") | (EXT="M2V") | (EXT="M4V") | (EXT="MKV") | (EXT="MOV") | (EXT="MP4") | (EXT="MPEG") | (EXT="MPG") | (EXT="MTS") | (EXT="OGG") | (EXT="TS") | (EXT="VOB") | (EXT="WEBM") | (EXT="WMV")';
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
    if (FieldIndex == 27) then
      par = "General;%Duration/String3%, %OverallBitRate/String%"
    elseif (FieldIndex == 28) then
      par = "Video;%BitRate/String%, %Width%x%Height%, %FrameRate%"
    else
      return nil;
    end
  else
    if (FieldIndex == 0) then
      par = "General;%InternetMediaType%";
    elseif (FieldIndex == 1) then
      par = "General;%Format%";
    elseif (FieldIndex == 2) then
      par = "General;%Duration%";
    elseif (FieldIndex == 3) then
      par = "General;%Duration/String3%";
    elseif (FieldIndex == 4) then
      par = "General;%Duration/String3%";
    elseif (FieldIndex == 5) then
      par = "General;%OverallBitRate%";
    elseif (FieldIndex == 6) then
      par = "General;%OverallBitRate%";
    elseif (FieldIndex == 7) then
      par = "General;%StreamCount%";
    elseif (FieldIndex == 8) then
      par = "General;%AudioCount%";
    elseif (FieldIndex == 9) then
      par = "General;%TextCount%";
    elseif (FieldIndex == 10) then
      par = "General;%ImageCount%";
    elseif (FieldIndex == 11) then
      par = "General;%MenuCount%";
    elseif (FieldIndex == 12) then
      par = "Video;%Format%";
    elseif (FieldIndex == 13) then
      par = "Video;%CodecID%";
    elseif (FieldIndex == 14) then
      par = "Video;%StreamSize%";
    elseif (FieldIndex == 15) then
      par = "Video;%BitRate%";
    elseif (FieldIndex == 16) then
      par = "Video;%BitRate%";
    elseif (FieldIndex == 17) then
      par = "Video;%Width%";
    elseif (FieldIndex == 18) then
      par = "Video;%Height%";
    elseif (FieldIndex == 19) then
      par = "Video;%Width%x%Height%";
    elseif (FieldIndex == 20) then
      par = "Video;%DisplayAspectRatio%";
    elseif (FieldIndex == 21) then
      par = "Video;%DisplayAspectRatio/String%";
    elseif (FieldIndex == 22) then
      par = "Video;%PixelAspectRatio%";
    elseif (FieldIndex == 23) then
      par = "Video;%FrameRate_Mode/String%";
    elseif (FieldIndex == 24) then
      par = "Video;%FrameRate%";
    elseif (FieldIndex == 25) then
      par = "Video;%FrameCount%";
    elseif (FieldIndex == 26) then
      par = "Video;%Rotation%";
    elseif (FieldIndex == 27) then
      par = "General;%Duration/String3%, %OverallBitRate/String%";
    elseif (FieldIndex == 28) then
      par = "Video;%BitRate/String%, %Width%x%Height%, %FrameRate%";
    else
      return nil;
    end
  end
  local handle = io.popen('mediainfo --Inform="' .. par .. '" "' .. FileName .. '"');
  local result = handle:read("*a");
  handle:close();
  result = string.gsub(result, "[\r\n]+", "");
  if (FieldIndex == 31) then
    return string.gsub(result, "(%d+:%d+:%d+)%.%d+", "%1") ;
  elseif (FieldIndex == 32) then
    return string.gsub(result, "^(, )", "") .. " FPS";
  elseif (FieldIndex == 3) then
    return string.match(result, "%d+:%d+:%d+");
  elseif (FieldIndex == 5) then
    if (result == '') then
      return nil;
    else
      if (UnitIndex == 0) then
        return result;
      elseif (UnitIndex == 1) then
        return result / 1000;
      elseif (UnitIndex == 2) then
        return result / 1000000;
      end
    end
  elseif (FieldIndex == 8) or (FieldIndex == 16) then
    if (result == '') then
      return nil;
    else
      if ((result / 1000000) < 1) then
        if ((result / 1000) < 1) then
          return result .. " Bps";
        else
          return string.format("%.2f", result / 1000) .. " KBps";
        end
      else
        return string.format("%.2f", result / 1000000) .. " MBps";
      end
    end
  elseif (FieldIndex == 15) then
    if (result == '') then
      return nil;
    else
      if (UnitIndex == 0) then
        return result;
      elseif (UnitIndex == 1) then
        return result / 1000;
      elseif (UnitIndex == 2) then
        return result / 1000000;
      end
    end
  elseif (FieldIndex == 20) or (FieldIndex == 22) or (FieldIndex == 24) then
    return string.gsub(result, "%.", string.match(tostring(0.5), "([^05+])"));
  else
    return result;
  end
  return nil;
end
