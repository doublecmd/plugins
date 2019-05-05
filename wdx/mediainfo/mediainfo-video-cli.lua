-- mediainfo-video-cli.lua
-- 2019.04.30
--
-- NOTE: If you want to change the number of fields, see the lines after "-- ATTENTION!":
--       also you mast to change some indexes!

-- FieldName, Units, FieldType, Context;Param
local fields = {
 {"General: Title",                                "", 8, "General;%Title%"},
 {"General: Format",                               "", 8, "General;%Format%"},
 {"General: Duration ms",                          "", 2, "General;%Duration%"},
 {"General: Duration HH:MM:SS",                    "", 8, "General;%Duration/String3%"},
 {"General: Duration HH:MM:SS.MMM",                "", 8, "General;%Duration/String3%"},
 {"General: Overall bit rate",        "Bps|KBps|MBps", 3, "General;%OverallBitRate%"},
 {"General: Overall bit rate, auto Bps/KBps/MBps", "", 8, "General;%OverallBitRate%"},
 {"General: Count of streams",                     "", 2, "General;%StreamCount%"},
 {"General: Number of audio streams",              "", 2, "General;%AudioCount%"},
 {"General: Number of text streams",               "", 2, "General;%TextCount%"},
 {"General: Number of image streams",              "", 2, "General;%ImageCount%"},
 {"General: Number of menu streams",               "", 2, "General;%MenuCount%"},
 {"Video: Format",                                 "", 8, "Video;%Format%"},
 {"Video: Format profile",                         "", 8, "Video;%Format_Profile%"},
 {"Video: Codec ID",                               "", 8, "Video;%CodecID%"},
 {"Video: Streamsize in bytes",                    "", 2, "Video;%StreamSize%"},
 {"Video: Bit rate",                  "Bps|KBps|MBps", 3, "Video;%BitRate%"},
 {"Video: Bit rate, auto Bps/KBps/MBps",           "", 8, "Video;%BitRate%"},
 {"Video: Width",                                  "", 2, "Video;%Width%"},
 {"Video: Height",                                 "", 2, "Video;%Height%"},
 {"Video: Width x Height",                         "", 8, "Video;%Width%x%Height%"},
 {"Video: Display Aspect ratio",                   "", 3, "Video;%DisplayAspectRatio%"},
 {"Video: DisplayAspectRatio (string)",            "", 8, "Video;%DisplayAspectRatio/String%"},
 {"Video: Pixel Aspect ratio",                     "", 3, "Video;%PixelAspectRatio%"},
 {"Video: Frame rate mode",                        "", 8, "Video;%FrameRate_Mode/String%"},
 {"Video: Frame rate",                             "", 3, "Video;%FrameRate%"},
 {"Video: Number of frames",                       "", 2, "Video;%FrameCount%"},
 {"Video: Bits/(Pixel*Frame)",                     "", 3, "Video;%Bits-(Pixel*Frame)%"},
 {"Video: Rotation",                               "", 2, "Video;%Rotation%"},
 {"Audio: Format",                                 "", 8, "Audio;%Format%"},
 {"Audio: Format profile",                         "", 8, "Audio;%Format_Profile%"},
 {"Audio: Codec ID",                               "", 8, "Audio;%CodecID%"},
 {"Audio: Streamsize in bytes",                    "", 2, "Audio;%StreamSize%"},
 {"Audio: Bit rate",                  "Bps|KBps|MBps", 3, "Audio;%BitRate%"},
 {"Audio: Bit rate, auto Bps/KBps/MBps",           "", 8, "Audio;%BitRate%"},
 {"Audio: Bit rate mode",                          "", 8, "Audio;%BitRate_Mode%"},
 {"Audio: Channel(s)",                             "", 2, "Audio;%Channel(s)%"},
 {"Audio: Sampling rate, KHz",                     "", 8, "Audio;%SamplingRate/String%"},
 {"Audio: Bit depth",                              "", 2, "Audio;%BitDepth%"},
 {"Audio: Delay in the stream",                    "", 2, "Audio;%Delay%"},
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
  if FieldIndex > 44 then return nil end
  local t = string.sub(FileName, string.len(FileName) - 3, -1)
  if (t == "/..") or (t == "\\..") then return nil end
  t = string.sub(t, 2, -1)
  if (t == "/.") or (t == "\\.") then return nil end
  -- ATTENTION!
  if flags == 1 then
    if FieldIndex < 42 then return nil end
  end
  local h = io.popen('mediainfo --Inform="' .. fields[FieldIndex + 1][4] .. '" "' .. FileName .. '"')
  if not h then return nil end
  local res = h:read("*a")
  h:close()
  res = string.gsub(res, "[\r\n]+", "")
  if FieldIndex == 3 then
    return string.match(res, "%d+:%d+:%d+")
  elseif (FieldIndex == 5) or (FieldIndex == 16) then
    if res == '' then
      return nil
    else
      if UnitIndex == 0 then
        return res
      elseif UnitIndex == 1 then
        return res / 1000
      elseif UnitIndex == 2 then
        return res / 1000000
      end
    end
  elseif (FieldIndex == 6) or (FieldIndex == 17) then
    if res == '' then
      return nil
    else
      if ((res / 1000000) < 1) then
        if ((res / 1000) < 1) then
          return res .. " Bps"
        else
          return string.format("%.2f", res / 1000) .. " KBps"
        end
      else
        return string.format("%.2f", res / 1000000) .. " MBps"
      end
    end
  elseif (FieldIndex == 21) or (FieldIndex == 23) or (FieldIndex == 25) or (FieldIndex == 27) then
    return string.gsub(res, "%.", string.match(tostring(0.5), "([^05+])"))
  else
    return res
  end
  return nil
end
