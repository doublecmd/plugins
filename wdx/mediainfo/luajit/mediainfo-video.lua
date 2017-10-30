-- mediainfo-video.lua
-- WDX, for columns set / search
-- 2017.10.05
--
-- Script will return some media info (for video).
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
  "General;%InternetMediaType%",
  "General;%Format%",
  "General;%Duration%",
  "General;%Duration/String3%",
  "General;%OverallBitRate%",
  "General;%StreamCount%",
  "General;%AudioCount%",
  "General;%TextCount%",
  "General;%ImageCount%",
  "General;%MenuCount%",
  "Video;%Format%",
  "Video;%CodecID%",
  "Video;%StreamSize%",
  "Video;%BitRate%",
  "Video;%Width%",
  "Video;%Height%",
  "Video;%DisplayAspectRatio%",
  "Video;%DisplayAspectRatio/String%",
  "Video;%PixelAspectRatio%",
  "Video;%FrameRate_Mode/String%",
  "Video;%FrameRate%",
  "Video;%FrameCount%",
  "Video;%Rotation%"
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
  if (FieldIndex > 30) then
    return nil;
  end
  if (filename ~= FileName) then
    -- open target File
    mediaInfo.MediaInfoA_Open (mi, FileName);
    for i=1, 31 do
      -- set option
      mediaInfo.MediaInfoA_Option(mi, "Inform", par_name[i]);
      -- get propety
      res[i] = ffi.string(mediaInfo.MediaInfoA_Inform(mi, 0));
    end
    -- close
    mediaInfo.MediaInfoA_Close (mi);
    filename = FileName;
  end
  --result = res[FieldIndex+1];
  if (FieldIndex == 0) then
    return res[1];
  elseif (FieldIndex == 1) then
    return res[2];
  elseif (FieldIndex == 2) then
    return res[3];
  elseif (FieldIndex == 3) then
    return string.match(res[4], "%d+:%d+:%d+");
  elseif (FieldIndex == 4) then
    return res[4];
  elseif (FieldIndex == 5) then
    if (res[5] == '') then
      return nil;
    else
      if (UnitIndex == 0) then
        return res[5];
      elseif (UnitIndex == 1) then
        return res[5] / 1000;
      elseif (UnitIndex == 2) then
        return res[5] / 1000000;
      end
    end
  elseif (FieldIndex == 6) then
    if (res[5] == '') then
      return nil;
    else
      if ((res[5] / 1000000) < 1) then
        if ((res[5] / 1000) < 1) then
          return res[5] .. " Bps";
        else
          return string.format("%.2f", res[5] / 1000) .. " KBps";
        end
      else
        return string.format("%.2f", res[5] / 1000000) .. " MBps";
      end
    end
  elseif (FieldIndex == 7) then
    return res[6];
  elseif (FieldIndex == 8) then
    return res[7];
  elseif (FieldIndex == 9) then
    return res[8];
  elseif (FieldIndex == 10) then
    return res[9];
  elseif (FieldIndex == 11) then
    return res[10];
  elseif (FieldIndex == 12) then
    return res[11];
  elseif (FieldIndex == 13) then
    return res[12];
  elseif (FieldIndex == 14) then
    return res[13];
  elseif (FieldIndex == 15) then
    if (res[14] == '') then
      return nil;
    else
      if (UnitIndex == 0) then
        return res[14];
      elseif (UnitIndex == 1) then
        return res[14] / 1000;
      elseif (UnitIndex == 2) then
        return res[14] / 1000000;
      end
    end
  elseif (FieldIndex == 16) then
    if (res[14] == '') then
      return nil;
    else
      if ((res[14] / 1000000) < 1) then
        if ((res[14] / 1000) < 1) then
          return res[14] .. " Bps";
        else
          return string.format("%.2f", res[14] / 1000) .. " KBps";
        end
      else
        return string.format("%.2f", res[14] / 1000000) .. " MBps";
      end
    end
  elseif (FieldIndex == 17) then
    return res[15];
  elseif (FieldIndex == 18) then
    return res[16];
  elseif (FieldIndex == 19) then
    return res[15] .. "x" .. res[16];
  elseif (FieldIndex == 20) then
    return tonumber(res[17]);
  elseif (FieldIndex == 21) then
    return res[18];
  elseif (FieldIndex == 22) then
    return tonumber(res[19]);
  elseif (FieldIndex == 23) then
    return res[20];
  elseif (FieldIndex == 24) then
    return tonumber(res[21]);
  elseif (FieldIndex == 25) then
    return res[22];
  elseif (FieldIndex == 26) then
    return res[23];
  end
  return nil;
end
