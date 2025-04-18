library OOInfo;

{$mode delphi}
{$include calling.inc}

uses
  FPCAdds, WdxPlugin, SProc, OOData, SysUtils, LazUTF8;

{.$R *.RES}

const
  _DetectString: PChar =
    'EXT="ODT" | EXT="ODS" | EXT="ODP" | EXT="ODG" | EXT="ODF" | EXT="ODB" | EXT="ODM" | '+
    'EXT="OTT" | EXT="OTH" | EXT="OTS" | EXT="OTG" | EXT="OTP" | '+
    'EXT="SXW" | EXT="SXC" | EXT="SXG" | EXT="SXI" | EXT="SXD" | EXT="SXM" | '+
    'EXT="STW" | EXT="STC" | EXT="STD" | EXT="STI"';

  _FieldsNum = 20;
  _Fields: array[0.._FieldsNum-1] of PChar = (
    'Generator',
    'Title',
    'Description',
    'Subject',
    'Initial creator',
    'Creator',
    'Creation date',
    'Modification date',
    'Keywords',
    'Language',
    'User info 1',
    'User info 2',
    'User info 3',
    'User info 4',
    'URL',
    'Print date',
    'Printed by',
    'Edition time',
    'Edition cycles',
    'Statistics'
    //,'Text' //must be the last field
    );

  _FieldTypes: array[0.._FieldsNum-1] of integer = (
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_stringw,
    ft_time,
    ft_numeric_32,
    ft_numeric_32
    //,ft_fulltext
    );

  _FieldUnits: array[0.._FieldsNum-1] of PChar = (
    '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '',
    'Tables|Images|Objects|Pages|Paragraphs|Words|Characters|Cells'{, ''});

//--------------------------------------------
procedure ContentGetDetectString(
  DetectString: pAnsiChar; maxlen: integer); dcpcall;
begin
  strlcopy(DetectString, PAnsiChar(AnsiString(_DetectString)), MaxLen);
end;

//--------------------------------------------
function ContentGetSupportedField(
  FieldIndex: integer;
  FieldName, Units: pAnsiChar;
  maxlen: integer): integer; dcpcall;
begin
  if (FieldIndex<0) or (FieldIndex>=_FieldsNum) then
    begin Result:= FT_NOMOREFIELDS; Exit end;

  strlcopy(FieldName, PAnsiChar(Ansistring(_Fields[FieldIndex])), MaxLen);
  strlcopy(Units, PAnsiChar(Ansistring(_FieldUnits[FieldIndex])), MaxLen);
  Result:= _FieldTypes[FieldIndex];
end;

//--------------------------------------------
function ContentGetValue(
  fn: pAnsiChar;
  FieldIndex, UnitIndex: integer;
  FieldValue: PByte;
  maxlen, flags: integer): integer; dcpcall;
begin
  Result:= ft_fieldempty;
end;

function ContentGetValueW(
  fn: pWideChar;
  FieldIndex, UnitIndex: integer;
  FieldValue: PByte;
  maxlen, flags: integer): integer; dcpcall;
var
  PVal: PInteger;
  PTime: PTimeFormat;
  //s, sAll: string;
begin
  (*
  //Text field
  if (FieldIndex=Pred(_FieldsNum)) then
    begin
    if UnitIndex=-1 then
    begin
      foooTextList.Clear;
      sAll:= '';
      Result:= ft_fieldempty;
      Exit
    end;

    //MessageBox(0, PChar(IntToStr(UnitIndex)), 'UnitIndex', MB_OK);
    if UnitIndex=0 then
    begin
      if not GetOODataAndText(fn) then
        begin Result:= FT_FILEERROR; Exit end;
    end;

    if sAll='' then
      sAll:= foooTextList.Text;
    s:= Copy(sAll, UnitIndex+1, MaxLen div 2);
    //messagebox(0, PChar(Format('%d'#13#13'%s', [UnitIndex, s])), 's',0);//

    if s='' then
      Result:= ft_fieldempty
    else
    begin
      strlcopy(PWideChar(FieldValue), PWideChar(Widestring(s)), MaxLen div 2);
      Result:= ft_fulltext;
    end;
    Exit;
    end;
    *)

  //ordinary fields
  if (FieldIndex<0) or (FieldIndex>=_FieldsNum) then
    begin Result:= FT_NOSUCHFIELD; Exit end;

  if not GetOOData(UTF16ToUTF8(UnicodeString(fn))) then
    begin Result:= FT_FILEERROR; Exit end;

  Result:= _FieldTypes[FieldIndex];
  PVal:= Pointer(FieldValue);
  PTime:= Pointer(FieldValue);

  {
    'Generator',
    'Title',
    'Description',
    'Subject',
    'Initial creator',
    'Creator',
    'Creation date',
    'Modification date',
    'Keywords',
    'Language',
    'User info 1',
    'User info 2',
    'User info 3',
    'User info 4',
    'URL',
    'Print date',
    'Printed by',
    'Edit cycles',
    'Statistics'
  }
  case FieldIndex of
     0: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooGenerator)), MaxLen div 2);
     1: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooTitle)), MaxLen div 2);
     2: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooDesc)), MaxLen div 2);
     3: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooSubject)), MaxLen div 2);
     4: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooInitialCreator)), MaxLen div 2);
     5: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooCreator)), MaxLen div 2);
     6: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooCreationDate)), MaxLen div 2);
     7: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooModifDate)), MaxLen div 2);
     8: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooKeywords)), MaxLen div 2);
     9: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooLanguage)), MaxLen div 2);
    10: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooUserInfo1)), MaxLen div 2);
    11: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooUserInfo2)), MaxLen div 2);
    12: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooUserInfo3)), MaxLen div 2);
    13: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooUserInfo4)), MaxLen div 2);
    14: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooURL)), MaxLen div 2);
    15: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooPrintDate)), MaxLen div 2);
    16: strlcopy(PWideChar(FieldValue), PWideChar(UTF8Decode(foooPrintedBy)), MaxLen div 2);
    17: Move(foooEditionTime, PTime^, SizeOf(TTimeFormat));
    18: PVal^:= foooEditionCycles;
    19:
      // Tables|Images|Objects|Pages|Paragraphs|Words|Characters|Cells
      case UnitIndex of
        0: PVal^:= foooNTables;
        1: PVal^:= foooNImages;
        2: PVal^:= foooNObjects;
        3: PVal^:= foooNPages;
        4: PVal^:= foooNParags;
        5: PVal^:= foooNWords;
        6: PVal^:= foooNChars;
        7: PVal^:= foooNCells;
      end;
  end;

  if (Result=ft_stringw) and (PWideChar(FieldValue)[0]=#0) then
    Result:= FT_FIELDEMPTY;
end;


exports
  ContentGetDetectString,
  ContentGetSupportedField,
  ContentGetValue,
  ContentGetValueW;

end.
