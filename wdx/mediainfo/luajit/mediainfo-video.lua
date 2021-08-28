-- mediainfo-video.lua
-- 2021.08.24
--
-- NOTE: If you want to change the number of fields, see the lines after "-- ATTENTION!":
--       also you mast to change some indexes!

--========================================
local ffi = require("ffi")
local mediaInfo = require("ffi-mediaInfo")
--========================================

-- FieldName, Units, FieldType, index of par_name, additional function
local fields = {
 {"General: Title",                                "", 8, 1, ""},
 {"General: Format",                               "", 8, 2, ""},
 {"General: Duration, ms",                         "", 2, 3, ""},
 {"General: Duration, s",                          "", 1, 3, "ToSeconds"},
 {"General: Duration, HH:MM:SS",                   "", 8, 4, "ShortTime"},
 {"General: Duration, HH:MM:SS.MMM",               "", 8, 4, ""},
 {"General: Overall bit rate",        "Bps|KBps|MBps", 3, 5, "BitRateUnits"},
 {"General: Overall bit rate, auto Bps/KBps/MBps", "", 8, 5, "BitRateFloat"},
 {"General: Count of streams",                     "", 2, 6, ""},
 {"General: Number of audio streams",              "", 2, 7, ""},
 {"General: Number of text streams",               "", 2, 8, ""},
 {"General: Number of image streams",              "", 2, 9, ""},
 {"General: Number of menu streams",               "", 2, 10, ""},
 {"Video: Format",                                 "", 8, 11, ""},
 {"Video: Format profile",                         "", 8, 12, ""},
 {"Video: Codec ID",                               "", 8, 13, ""},
 {"Video: Streamsize in bytes",                    "", 2, 14, ""},
 {"Video: Bit rate",                  "Bps|KBps|MBps", 3, 15, "BitRateUnits"},
 {"Video: Bit rate, auto Bps/KBps/MBps",           "", 8, 15, "BitRateFloat"},
 {"Video: Width",                                  "", 2, 16, ""},
 {"Video: Height",                                 "", 2, 17, ""},
 {"Video: Display aspect ratio",                   "", 3, 18, "tonumber"},
 {"Video: Display aspect ratio (string)",          "", 8, 19, ""},
 {"Video: Pixel aspect ratio",                     "", 3, 20, "tonumber"},
 {"Video: Frame rate mode",                        "", 8, 21, ""},
 {"Video: Frame rate",                             "", 3, 22, "tonumber"},
 {"Video: Number of frames",                       "", 2, 23, ""},
 {"Video: Bits/(Pixel*Frame)",                     "", 3, 24, "tonumber"},
 {"Video: Rotation",                               "", 2, 25, ""},
 {"Audio: Format",                                 "", 8, 26, ""},
 {"Audio: Format profile",                         "", 8, 27, ""},
 {"Audio: Codec ID",                               "", 8, 28, ""},
 {"Audio: Streamsize in bytes",                    "", 2, 29, ""},
 {"Audio: Bit rate",                  "Bps|KBps|MBps", 3, 30, "BitRateUnits"},
 {"Audio: Bit rate, auto Bps/KBps/MBps",           "", 8, 30, "BitRateFloat"},
 {"Audio: Bit rate mode",                          "", 8, 31, ""},
 {"Audio: Channel(s)",                             "", 2, 32, ""},
 {"Audio: Sampling rate, KHz",                     "", 8, 33, ""},
 {"Audio: Bit depth",                              "", 2, 34, ""},
 {"Audio: Delay in the stream",                    "", 2, 35, ""},
 {"Audio: Title",                                  "", 8, 36, ""},
 {"Audio: Language",                               "", 8, 37, ""}
}
local mi = nil
local par_name = {
 "General;%Title%",
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
 "Video;%Format_Profile%",
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
 "Video;%Bits-(Pixel*Frame)%",
 "Video;%Rotation%",
 "Audio;%Format%",
 "Audio;%Format_Profile%",
 "Audio;%CodecID%",
 "Audio;%StreamSize%",
 "Audio;%BitRate%",
 "Audio;%BitRate_Mode%",
 "Audio;%Channel(s)%",
 "Audio;%SamplingRate/String%",
 "Audio;%BitDepth%",
 "Audio;%Delay%",
 "Audio;%Title%",
 "Audio;%Language%"
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
  if fields[FieldIndex + 1] ~= nil then
    return fields[FieldIndex + 1][1], fields[FieldIndex + 1][2], fields[FieldIndex + 1][3]
  end
  return "", "", 0
end

function ContentGetDetectString()
  return '(EXT="3G2")|(EXT="3GP")|(EXT="ASF")|(EXT="AVI")|(EXT="DIVX")|(EXT="FLV")|(EXT="M2T")|(EXT="M2TS")|(EXT="M2V")|(EXT="M4V")|(EXT="MKV")|(EXT="MOV")|(EXT="MP4")|(EXT="MPEG")|(EXT="MPG")|(EXT="MTS")|(EXT="OGG")|(EXT="TS")|(EXT="VOB")|(EXT="WEBM")|(EXT="WMV")'
end

function ContentGetValue(FileName, FieldIndex, UnitIndex, flags)
  -- ATTENTION!
  if FieldIndex > 41 then return nil end
  local t = string.sub(FileName, string.len(FileName) - 3, -1)
  if (t == "/..") or (t == "\\..") then return nil end
  t = string.sub(t, 2, -1)
  if (t == "/.") or (t == "\\.") then return nil end
  if filename ~= FileName then
    -- Open target File
    mediaInfo.MediaInfoA_Open(mi, FileName)
    -- ATTENTION!
    for i = 1, 37 do
      -- Set option
      mediaInfo.MediaInfoA_Option(mi, "Inform", par_name[i])
      -- Get propety
      res[i] = ffi.string(mediaInfo.MediaInfoA_Inform(mi, 0))
    end
    -- Close
    mediaInfo.MediaInfoA_Close(mi)
    filename = FileName
  end
  -- Result = res[FieldIndex + 1]
  if fields[FieldIndex + 1][5] == "tonumber" then
    return tonumber(res[fields[FieldIndex + 1][4]])
  elseif fields[FieldIndex + 1][5] == "ShortTime" then
    return ShortTime(fields[FieldIndex + 1][4])
  elseif fields[FieldIndex + 1][5] == "BitRateUnits" then
    return BitRateUnits(fields[FieldIndex + 1][4], UnitIndex)
  elseif fields[FieldIndex + 1][5] == "BitRateFloat" then
    return BitRateFloat(fields[FieldIndex + 1][4])
  elseif fields[FieldIndex + 1][5] == "ToSeconds" then
    return ToSeconds(fields[FieldIndex + 1][4])
  else
    return res[fields[FieldIndex + 1][4]]
  end
  return nil
end

function ShortTime(c)
  if res[c] == '' then return nil end
  return string.match(res[c], "%d+:%d+:%d+")
end

function BitRateUnits(c, u)
  if res[c] == '' then return nil end
  if u == 0 then
    return res[c]
  elseif u == 1 then
    return res[c] / 1000
  elseif u == 2 then
    return res[c] / 1000000
  end
  return nil
end

function BitRateFloat(c)
  if res[c] == '' then return nil end
  if ((res[c] / 1000000) < 1) then
    if ((res[c] / 1000) < 1) then
      return res[c] .. " Bps"
    else
      return string.format("%.2f", res[c] / 1000) .. " KBps"
    end
  else
    return string.format("%.2f", res[c] / 1000000) .. " MBps"
  end
  return nil
end

function ToSeconds(c)
  if res[c] == '' then return nil end
  return math.floor(res[c] / 1000)
end
