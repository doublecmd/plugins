-- mediainfo-video-cli.lua
-- 2026.06.03
--
-- NOTE: If you want to change the number of fields, see the lines after "-- ATTENTION!":
--       also you mast to change some indexes!

-- FieldName, Units, FieldType, Context;Param
local fields = {
{"General: Title",                                "", 8, "General;%Title%"},
{"General: Format",                               "", 8, "General;%Format%"},
{"General: Duration, ms",                         "", 2, "General;%Duration%"},
{"General: Duration, s",                          "", 1, "General;%Duration%"},
{"General: Duration, HH:MM:SS",                   "", 8, "General;%Duration/String3%"},
{"General: Duration, HH:MM:SS.MMM",               "", 8, "General;%Duration/String3%"},
{"General: Overall bit rate",        "Bps|KBps|MBps", 3, "General;%OverallBitRate%"},
{"General: Overall bit rate, auto Bps/KBps/MBps", "", 8, "General;%OverallBitRate%"},
{"General: Count of streams",                     "", 1, "General;%StreamCount%"},
{"General: Number of audio streams",              "", 1, "General;%AudioCount%"},
{"General: Number of text streams",               "", 1, "General;%TextCount%"},
{"General: Number of image streams",              "", 1, "General;%ImageCount%"},
{"General: Number of menu streams",               "", 1, "General;%MenuCount%"},
{"Video: Format",                                 "", 8, "Video;%Format%"},
{"Video: Format profile",                         "", 8, "Video;%Format_Profile%"},
{"Video: Codec ID",                               "", 8, "Video;%CodecID%"},
{"Video: Streamsize in bytes",                    "", 2, "Video;%StreamSize%"},
{"Video: Bit rate",                  "Bps|KBps|MBps", 3, "Video;%BitRate%"},
{"Video: Bit rate, auto Bps/KBps/MBps",           "", 8, "Video;%BitRate%"},
{"Video: Width",                                  "", 1, "Video;%Width%"},
{"Video: Height",                                 "", 1, "Video;%Height%"},
{"Video: Width x Height",                         "", 8, "Video;%Width%x%Height%"},
{"Video: Display Aspect ratio",                   "", 3, "Video;%DisplayAspectRatio%"},
{"Video: DisplayAspectRatio (string)",            "", 8, "Video;%DisplayAspectRatio/String%"},
{"Video: Pixel Aspect ratio",                     "", 3, "Video;%PixelAspectRatio%"},
{"Video: Frame rate mode",                        "", 8, "Video;%FrameRate_Mode/String%"},
{"Video: Frame rate",                             "", 3, "Video;%FrameRate%"},
{"Video: Number of frames",                       "", 2, "Video;%FrameCount%"},
{"Video: Bits/(Pixel*Frame)",                     "", 3, "Video;%Bits-(Pixel*Frame)%"},
{"Video: Rotation",                               "", 1, "Video;%Rotation%"},
{"Audio: Format",                                 "", 8, "Audio;%Format%"},
{"Audio: Format profile",                         "", 8, "Audio;%Format_Profile%"},
{"Audio: Codec ID",                               "", 8, "Audio;%CodecID%"},
{"Audio: Streamsize in bytes",                    "", 2, "Audio;%StreamSize%"},
{"Audio: Bit rate",                  "Bps|KBps|MBps", 3, "Audio;%BitRate%"},
{"Audio: Bit rate, auto Bps/KBps/MBps",           "", 8, "Audio;%BitRate%"},
{"Audio: Bit rate mode",                          "", 8, "Audio;%BitRate_Mode%"},
{"Audio: Channel(s)",                             "", 1, "Audio;%Channel(s)%"},
{"Audio: Sampling rate, Hz",                      "", 1, "Audio;%SamplingRate%"},
{"Audio: Bit depth",                              "", 1, "Audio;%BitDepth%"},
{"Audio: Delay in the stream",                    "", 1, "Audio;%Delay%"},
{"Audio: Title",                                  "", 8, "Audio;%Title%"},
{"Audio: Language",                               "", 8, "Audio;%Language%"},
{"General:",                                      "", 8, "General;%Duration/String3%, %OverallBitRate/String%"},
{"Video:",                                        "", 8, "Video;%Format%, %BitRate/String%, %Width%x%Height%, %FrameRate%"},
{"Audio:",                                        "", 8, "Audio;%Format%, %BitRate/String%, %SamplingRate/String%, %BitDepth/String%, %Channel(s)%"}
}

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
  if FieldIndex > 45 then return nil end
  local t = string.sub(FileName, string.len(FileName) - 3, -1)
  if (t == "/..") or (t == "\\..") then return nil end
  t = string.sub(t, 2, -1)
  if (t == "/.") or (t == "\\.") then return nil end
  -- ATTENTION!
  if flags == 1 then
    if FieldIndex < 43 then return nil end
  end
  local h = io.popen('mediainfo --Inform="' .. fields[FieldIndex + 1][4] .. '" "' .. FileName .. '"')
  if not h then return nil end
  local res = h:read("*a")
  h:close()
  res = string.gsub(res, "[\r\n]+", "")
  if res == "" then return nil end
  if (FieldIndex == 2) or (FieldIndex == 3) then
    t = string.match(res, "^(%d+)")
    if FieldIndex == 3 then
      return math.floor(tonumber(t) / 1000)
    else
      return tonumber(t)
    end
  elseif FieldIndex == 4 then
    return string.match(res, "%d+:%d+:%d+")
  elseif (FieldIndex == 6) or (FieldIndex == 17) then
    if UnitIndex == 0 then
      return tonumber(res)
    elseif UnitIndex == 1 then
      return tonumber(res) / 1000
    elseif UnitIndex == 2 then
      return tonumber(res) / 1000000
    end
  elseif (FieldIndex == 7) or (FieldIndex == 18) then
    local n = tonumber(res)
    if (n / 1000000) < 1 then
      if (n / 1000) < 1 then
        return res .. " Bps"
      else
        return string.format("%.2f", n / 1000) .. " KBps"
      end
    else
      return string.format("%.2f", n / 1000000) .. " MBps"
    end
  elseif (FieldIndex == 22) or (FieldIndex == 24) or (FieldIndex == 26) or (FieldIndex == 28) then
    t = string.gsub(res, "%.", string.match(tostring(0.5), "([^05+])"))
    return tonumber(t)
  else
    if fields[FieldIndex + 1][3] < 4 then
      return tonumber(res)
    else
      return res
    end
  end
end
