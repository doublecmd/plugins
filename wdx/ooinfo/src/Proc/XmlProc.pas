{$I-}
unit XmlProc;

interface

uses Classes;

procedure ReadXmlFile(const fn: string);
function GetXmlEncoding: string;
function GetXmlTag(const tagName: string): string;
function GetXmlTagParam(const tagName, tagParam: string): string;
function GetXmlTagList(const tagName: string): string;
function GetXmlText: string; overload;
procedure GetXmlText(List: TStringList); overload;

// Function for FB2 only:
function GetXmlTagParamAndValue(
  var SContent: string;
  const tagName, tagParam: string;
  var SParamValue, STagValue: string): boolean;

type
  TTimeFormat = record
    wHour, wMinute, wSecond: word;
  end;

procedure GetXmlTimeTag(const tagName: string; var time: TTimeFormat);
procedure GetXmlUserTags(var tag1, tag2, tag3, tag4: string);
procedure GetXmlDocStat(var nTables, nImages, nObjects, nPages, nParags, nWords, nChars, nCells: integer);

type
  TXmlProgress = procedure(const Msg: string; Perc: integer);

var
  XmlProgress: TXmlProgress = nil;

const
  ssXmlMsgReadingInfo = 'Reading document properties';
  ssXmlMsgReadingText = 'Reading document text';
  ssXmlMsgDecoding = 'Decoding document';


implementation

uses
  SysUtils, SProc;

type
  TXmlEncoding = (vencAscii, vencUTF8);

var
  XmlCont: string = '';
  XmlEncoding: TXmlEncoding;

//-----------------------------------------
const
  sDecodeRec: array[1..5] of TDecodeRec =
    ((SFrom: '&quot;'; STo: '"'),
     (SFrom: '&amp;';  STo: '&'),
     (SFrom: '&apos;'; STo: ''''),
     (SFrom: '&lt;';   STo: '<'),
     (SFrom: '&gt;';   STo: '>'));

{
procedure DecodeProgress(N: integer);
begin
  if Assigned(XmlProgress) then
    XmlProgress(ssXmlMsgDecoding, N);
end;
}

function Decode(const s: string): string;
begin
  Result:= s;
//  if XmlEncoding=vencUTF8 then
//    Result:= UTF8Decode(AnsiString(Result));
  Result:= SDecode(Result, sDecodeRec, nil{DecodeProgress});
end;

//-----------------------------------------
procedure ReadXmlFile(const fn: string);
var
  Enc: string;
begin
  XmlCont:= fn;
  SReplaceAll(XmlCont, '<text:line-break/>', #13#10);

  Enc:= GetXmlEncoding;
  if UpperCase(Enc)= 'UTF-8'
    then XmlEncoding:= vencUTF8
    else XmlEncoding:= vencAscii;
end;

//-----------------------------------------
// <?xml version="1.0" encoding="UTF-8" ?> 
function GetXmlEncoding: string;
var
  s: string;
  tagStart: string;
begin
  Result:= '';
  s:= XmlCont;
  tagStart:= '<?xml ';
  if Pos(tagStart, s)>0 then
    begin
    s:= SDeleteTo(s, tagStart);
    s:= SDeleteFrom(s, '?>');
    if Pos('encoding', s)=0 then Exit;
    s:= SDeleteTo(s, 'encoding');
    Result:= SBetween(s, '"', '"');
    end;
end;

//-----------------------------------------
function GetXmlTag(const tagName: string): string;
var
  s: string;
  tagStart: string;
begin
  Result:= '';
  s:= XmlCont;
  tagStart:= '<'+tagName+'>';
  if Pos(tagStart, s)>0 then
    begin
    s:= SDeleteTo(s, tagStart);
    Result:= SDeleteFrom(s, '<');
    Result:= Decode(Result);
    end;
end;

//-----------------------------------------
// <meta:auto-reload xlink:href="http://alextpp.narod.ru/" meta:delay="PT5S" /> 
function GetXmlTagParam(const tagName, tagParam: string): string;
var
  s: string;
  tagStart: string;
begin
  Result:= '';
  s:= XmlCont;
  tagStart:= '<'+tagName+' ';
  if Pos(tagStart, s)>0 then
    begin
    s:= SDeleteTo(s, tagStart);
    s:= SDeleteTo(s, tagParam);
    s:= SDeleteFrom(s, '/>');
    Result:= SBetween(s, '"', '"');
    end;
end;

//-----------------------------------------
// <binary content-type="image/png" id="cover.png">......</binary>
function GetXmlTagParamAndValue(
  var SContent: string;
  const tagName, tagParam: string;
  var SParamValue, STagValue: string): boolean;
var
  s, tagStart, tagEnd: string;
  n1, n2: integer;
begin
  Result:= false;

  tagStart:= '<'+tagName+' ';
  tagEnd:= '</'+tagName+'>';
  n1:= Pos(tagStart, SContent);  if n1=0 then Exit;
  n2:= PosFrom(tagEnd, SContent, n1);  if n2=0 then Exit;

  //Delete all before found tag - this will speedup parsing:
  s:= Copy(SContent, n1+1, n2-n1);
  Delete(SContent, 1, n2-1);

  STagValue:= SBetween(s, '>', '<');
  if STagValue='' then Exit;

  tagStart:= ' '+tagParam+'=';
  tagEnd:= '>';
  SParamValue:= SDeleteTo(s, tagStart);
  SParamValue:= SDeleteFrom(SParamValue, tagEnd);
  SParamValue:= SBetween(SParamValue, '"', '"');
  if SParamValue='' then Exit;

  Result:= true;
end;

//-----------------------------------------
function GetXmlTagList(const tagName: string): string;
var
  s, ss: string;
  tagStart, tagEnd: string;
begin
  Result:= '';
  s:= XmlCont;
  tagStart:= '<'+tagName+'>';
  tagEnd:= '</'+tagName+'>';
  repeat
    if Pos(tagStart, s)=0 then Break;
    s:= SDeleteTo(s, tagStart);
    ss:= SDeleteFrom(s, '<');
    if Result=''
      then Result:= ss
      else Result:= Result+', '+ss;
    s:= SDeleteTo(s, tagEnd);
  until false;
  Result:= Decode(Result);
end;

//-----------------------------------------
// <meta:user-defined meta:name="Info 1">My info 1</meta:user-defined> 
procedure GetXmlUserTags(var tag1, tag2, tag3, tag4: string);
var
  s: string;

  function GetVal: string;
  var
    tagStart: string;
  begin
    Result:= '';
    tagStart:= '<meta:user-defined ';
    if Pos(tagStart, s)>0 then
      begin
      s:= SDeleteTo(s, tagStart);
      //tagName:= Decode(SBetween(s, '"', '"'));
      Result:= Decode(SBetween(s, '>', '<'));
      s:= SDeleteTo(s, '</');
      end;
  end;

begin
  s:= XmlCont;
  tag1:= GetVal;
  tag2:= GetVal;
  tag3:= GetVal;
  tag4:= GetVal;
end;

//-----------------------------------------
{
<meta:document-statistic
meta:table-count="0"
meta:cell-count="5"
meta:image-count="0"
meta:object-count="0"
meta:page-count="1"
meta:paragraph-count="21"
meta:word-count="126"
meta:character-count="952"/>
}
procedure GetXmlDocStat(var nTables, nImages, nObjects, nPages, nParags, nWords, nChars, nCells: integer);
var
  s: string;

  function TagVal(const name: string): integer;
  begin
    if Pos(name, s)=0
      then Result:= 0
      else Result:= StrToIntDef(SBetween(SDeleteTo(s, name), '"', '"'), 0);
  end;

var
  tagStart: string;
begin
  s:= XmlCont;
  tagStart:= '<meta:document-statistic ';
  if Pos(tagStart, s)>0 then
    begin
    s:= SDeleteTo(s, tagStart);
    s:= SDeleteFrom(s, '/>');
    end;
  nTables:= TagVal('meta:table-count');
  nImages:= TagVal('meta:image-count');
  nObjects:= TagVal('meta:object-count');
  nPages:= TagVal('meta:page-count');
  nParags:= TagVal('meta:paragraph-count');
  nWords:= TagVal('meta:word-count');
  nChars:= TagVal('meta:character-count');
  nCells:= TagVal('meta:cell-count');
end;

//-----------------------------------------
procedure GetXmlTimeTag(const tagName: string; var time: TTimeFormat);

  // PT11M42S, PT23H6M41S
  function GetVal(s: string; const id: string): integer;
  var
    n: integer;
  begin
    s:= SDeleteFrom(s, id);
    n:= Length(s);
    while (n>0) and (s[n] in ['0'..'9']) do Dec(n);
    Delete(s, 1, n);
    Result:= StrToIntDef(s, 0);
  end;

var
  s: string;
begin
  s:= GetXmlTag(tagName);
  time.wHour:= GetVal(s, 'H');
  time.wMinute:= GetVal(s, 'M');
  time.wSecond:= GetVal(s, 'S');
end;

//-----------------------------------------
function GetXmlText: string;
var
  s: string absolute XmlCont;
  tagName, tagCont: string;
  n1, n2, nStart: integer;
begin
  Result:= '';
  nStart:= 1;
  if Assigned(XmlProgress) then
    XmlProgress(ssXmlMsgReadingText, 0);

  repeat
    n1:= Pos2From('<text:p', '<text:h', s, nStart);
    if n1=0 then Break;
    n2:= Pos2From(' ', '>', s, n1);

    tagName:= Copy(s, n1+1, n2-n1-1);
    //Writeln('tagName: "', tagName, '"');

    n1:= PosFrom('>', s, n1);
    if Copy(s, n1-1, 2)='/>'
      then n2:= n1+1
      else n2:= PosFrom('</'+tagname+'>', s, n1);

    tagCont:= Copy(s, n1+1, n2-n1-1);
    //Writeln('tagCont: "', tagCont, '"');

    //Strip tags and decode text
    tagCont:= SDeleteTags(tagCont);
    tagCont:= Decode(tagCont);

    Result:= Result+tagCont+#13#10;
    nStart:= n2;

    if Assigned(XmlProgress) then
      if Length(s)>0 then
        XmlProgress(ssXmlMsgReadingText, nStart*100 div Length(s));
  until false;

  //Messagebox(0, PChar(Result), 'T', 0);
end;

//-----------------------------------------
procedure GetXmlText(List: TStringList); overload;
var
  s: string absolute XmlCont;
  tagName, tagCont: string;
  n1, n2, nStart: integer;
begin
  if not Assigned(List) then Exit;
  List.BeginUpdate;
  List.Clear;

  nStart:= 1;
  if Assigned(XmlProgress) then
    XmlProgress(ssXmlMsgReadingText, 0);

  repeat
    n1:= Pos2From('<text:p', '<text:h', s, nStart);
    if n1=0 then Break;
    n2:= Pos2From(' ', '>', s, n1);

    tagName:= Copy(s, n1+1, n2-n1-1);
    //Writeln('tagName: "', tagName, '"');

    n1:= PosFrom('>', s, n1);
    if Copy(s, n1-1, 2)='/>'
      then n2:= n1+1
      else n2:= PosFrom('</'+tagname+'>', s, n1);

    tagCont:= Copy(s, n1+1, n2-n1-1);
    //Writeln('tagCont: "', tagCont, '"');

    //Strip tags and decode text
    tagCont:= SDeleteTags(tagCont);
    tagCont:= Decode(tagCont);

    List.Add(tagCont);
    nStart:= n2;

    if Assigned(XmlProgress) then
      if Length(s)>0 then
        XmlProgress(ssXmlMsgReadingText, nStart*100 div Length(s));
  until false;

  List.EndUpdate;
  //Messagebox(0, PChar(List.Text), 'T', 0);
end;


end.
