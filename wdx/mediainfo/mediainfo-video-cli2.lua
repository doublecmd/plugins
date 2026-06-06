-- mediainfo-video-cli2.lua
-- 2026.06.03
--
-- NOTE: If you want to change the number of fields, see the lines after "-- ATTENTION!":
--       also you mast to change some indexes!

-- FieldName, Units, FieldType, index of par_name, additional function
local fields = {
{"General: Title",                                "", 8, 1, ""},
{"General: Format",                               "", 8, 2, ""},
{"General: Duration, ms",                         "", 2, 3, "ToNumber"},
{"General: Duration, s",                          "", 1, 3, "ToSeconds"},
{"General: Duration, HH:MM:SS",                   "", 8, 4, "ShortTime"},
{"General: Duration, HH:MM:SS.MMM",               "", 8, 4, ""},
{"General: Overall bit rate",        "Bps|KBps|MBps", 3, 5, "BitRateUnits"},
{"General: Overall bit rate, auto Bps/KBps/MBps", "", 8, 5, "BitRateFloat"},
{"General: Count of streams",                     "", 1, 6, "ToNumber"},
{"General: Number of audio streams",              "", 1, 7, "ToNumber"},
{"General: Number of text streams",               "", 1, 8, "ToNumber"},
{"General: Number of image streams",              "", 1, 9, "ToNumber"},
{"General: Number of menu streams",               "", 1, 10, "ToNumber"},
{"Video: Format",                                 "", 8, 11, ""},
{"Video: Format profile",                         "", 8, 12, ""},
{"Video: Codec ID",                               "", 8, 13, ""},
{"Video: Streamsize in bytes",                    "", 2, 14, "ToNumber"},
{"Video: Bit rate",                  "Bps|KBps|MBps", 3, 15, "BitRateUnits"},
{"Video: Bit rate, auto Bps/KBps/MBps",           "", 8, 15, "BitRateFloat"},
{"Video: Width",                                  "", 1, 16, "ToNumber"},
{"Video: Height",                                 "", 1, 17, "ToNumber"},
{"Video: Display Aspect ratio",                   "", 3, 18, "StringToFloat"},
{"Video: DisplayAspectRatio (string)",            "", 8, 19, "ToNumber"},
{"Video: Pixel Aspect ratio",                     "", 3, 20, "StringToFloat"},
{"Video: Frame rate mode",                        "", 8, 21, ""},
{"Video: Frame rate",                             "", 3, 22, "StringToFloat"},
{"Video: Number of frames",                       "", 2, 23, "ToNumber"},
{"Video: Bits/(Pixel*Frame)",                     "", 3, 24, "StringToFloat"},
{"Video: Rotation",                               "", 1, 25, "ToNumber"},
{"Audio: Format",                                 "", 8, 26, ""},
{"Audio: Format profile",                         "", 8, 27, ""},
{"Audio: Codec ID",                               "", 8, 28, ""},
{"Audio: Streamsize in bytes",                    "", 2, 29, "ToNumber"},
{"Audio: Bit rate",                  "Bps|KBps|MBps", 3, 30, "BitRateUnits"},
{"Audio: Bit rate, auto Bps/KBps/MBps",           "", 8, 30, "BitRateFloat"},
{"Audio: Bit rate mode",                          "", 8, 31, ""},
{"Audio: Channel(s)",                             "", 1, 32, "ToNumber"},
{"Audio: Sampling rate, Hz",                      "", 1, 33, "ToNumber"},
{"Audio: Bit depth",                              "", 1, 34, "ToNumber"},
{"Audio: Delay in the stream",                    "", 1, 35, "ToNumber"},
{"Audio: Title",                                  "", 8, 36, ""},
{"Audio: Language",                               "", 8, 37, ""}
}
local par_name = {
{"General", "%Title%"},
{"General", "%Format%"},
{"General", "%Duration%"},
{"General", "%Duration/String3%"},
{"General", "%OverallBitRate%"},
{"General", "%StreamCount%"},
{"General", "%AudioCount%"},
{"General", "%TextCount%"},
{"General", "%ImageCount%"},
{"General", "%MenuCount%"},
{"Video", "%Format%"},
{"Video", "%Format_Profile%"},
{"Video", "%CodecID%"},
{"Video", "%StreamSize%"},
{"Video", "%BitRate%"},
{"Video", "%Width%"},
{"Video", "%Height%"},
{"Video", "%DisplayAspectRatio%"},
{"Video", "%DisplayAspectRatio/String%"},
{"Video", "%PixelAspectRatio%"},
{"Video", "%FrameRate_Mode/String%"},
{"Video", "%FrameRate%"},
{"Video", "%FrameCount%"},
{"Video", "%Bits-(Pixel*Frame)%"},
{"Video", "%Rotation%"},
{"Audio", "%Format%"},
{"Audio", "%Format_Profile%"},
{"Audio", "%CodecID%"},
{"Audio", "%StreamSize%"},
{"Audio", "%BitRate%"},
{"Audio", "%BitRate_Mode%"},
{"Audio", "%Channel(s)%"},
{"Audio", "%SamplingRate%"},
{"Audio", "%BitDepth%"},
{"Audio", "%Delay%"},
{"Audio", "%Title%"},
{"Audio", "%Language%"}
}
local g, v, a = "General;", "Video;", "Audio;"
local res = {}
local filename

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
  if g == "General;" then
    -- ATTENTION!
    for i = 1, 37 do
      if par_name[i][1] == "General" then
        g = g .. par_name[i][2] .. "|"
      elseif par_name[i][1] == "Video" then
        v = v .. par_name[i][2] .. "|"
      elseif par_name[i][1] == "Audio" then
        a = a .. par_name[i][2] .. "|"
      end
    end
  end
  if filename ~= FileName then
    t = GetInfo(FileName, g)
    if t == nil then return nil end
    local c = AddToTable(t, 1)
    c = AddToTable(GetInfo(FileName, v), c)
    AddToTable(GetInfo(FileName, a), c)
    -- Workaround
    if res[3] ~= "" then res[3] = string.match(res[3], "^(%d+)") end
    filename = FileName
  end
  -- Result = res[FieldIndex + 1]
  if fields[FieldIndex + 1][5] == "ToNumber" then
    return ToNumber(fields[FieldIndex + 1][4])
  elseif fields[FieldIndex + 1][5] == "StringToFloat" then
    return StringToFloat(fields[FieldIndex + 1][4])
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

function GetInfo(fn, p)
  local h = io.popen('mediainfo --Inform="' .. p .. '" "' .. fn .. '"')
  if not h then return nil end
  local r = h:read("*a")
  h:close()
  return string.gsub(r, "[\r\n]+$", "")
end

function AddToTable(s, i)
  local n1, t
  local n2 = 1
  while true do
    n1 = string.find(s, "|", n2, true)
    if n1 == nil then break end
    res[i] = string.sub(s, n2, n1 - 1)
    n2 = n1 + 1
    i = i + 1
  end
  return i
end

function ToNumber(c)
  if res[c] == "" then return nil end
  return tonumber(res[c])
end

function StringToFloat(c)
  if res[c] == "" then return nil end
  local t = string.gsub(res[c], "%.", string.match(tostring(0.5), "([^05+])"))
  return tonumber(t)
end

function ShortTime(c)
  if res[c] == "" then return nil end
  return string.match(res[c], "%d+:%d+:%d+")
end

function BitRateUnits(c, u)
  if res[c] == "" then return nil end
  if u == 0 then
    return tonumber(res[c])
  elseif u == 1 then
    return tonumber(res[c]) / 1000
  elseif u == 2 then
    return tonumber(res[c]) / 1000000
  end
  return nil
end

function BitRateFloat(c)
  if res[c] == "" then return nil end
  local n = tonumber(res[c])
  if (n / 1000000) < 1 then
    if (n / 1000) < 1 then
      return res[c] .. " Bps"
    else
      return string.format("%.2f", n / 1000) .. " KBps"
    end
  else
    return string.format("%.2f", n / 1000000) .. " MBps"
  end
  return nil
end

function ToSeconds(c)
  if res[c] == "" then return nil end
  return math.floor(tonumber(res[c]) / 1000)
end
