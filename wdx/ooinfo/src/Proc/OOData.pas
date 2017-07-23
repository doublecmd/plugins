unit OOData;

{$IFDEF FPC}
  {$MODE Delphi}
{$ENDIF}

interface

uses
  XmlProc, Classes;

var
  foooGenerator,
  foooTitle,
  foooDesc,
  foooSubject,
  foooInitialCreator,
  foooCreator,
  foooCreationDate,
  foooModifDate,
  foooKeywords,
  foooLanguage,
  foooUserInfo1,
  foooUserInfo2,
  foooUserInfo3,
  foooUserInfo4,
  foooURL,
  foooPrintDate,
  foooPrintedBy: string;
  foooEditionTime: TTimeFormat;
  foooEditionCycles: integer;
  foooNTables, foooNImages, foooNObjects, foooNPages, foooNParags, foooNWords, foooNChars, foooNCells: integer;
  foooTextList: TStringList;

function GetOOData(const fn: string): boolean;
//function GetOOText(const fn: string): boolean; //deprecated
function GetOODataAndText(const fn: string): boolean;

implementation

uses
  SysUtils, SProc, Unzip, ZipUtils;

const
  meta_xml = 'meta.xml';
  content_xml = 'content.xml';

  function ExtractFile(ArchiveName, FileName: PAnsiChar; out Data: String): Boolean;
  var
    iError: LongInt;
    xpiFile: unzFile = nil;
    rdfInfo: unz_file_info;
    FileBuffer: PAnsiChar = nil;
  begin
    Result:= False;
    xpiFile:= unzOpen(ArchiveName);
    if Assigned(xpiFile) then
    try
      iError:= unzLocateFile(xpiFile, FileName, 0);
      if iError <> UNZ_OK then Exit;
      iError:= unzGetCurrentFileInfo(xpiFile, @rdfInfo, nil, 0, nil, 0, nil, 0);
      if iError <> UNZ_OK then Exit;
      FileBuffer:= AllocMem(rdfInfo.uncompressed_size);
      if FileBuffer = nil then Exit;
      iError := unzOpenCurrentFile(xpiFile);
      if iError <> UNZ_OK then Exit;
      iError := unzReadCurrentFile(xpiFile, FileBuffer, rdfInfo.uncompressed_size);
      if iError < 0 then Exit;
      SetString(Data, FileBuffer, rdfInfo.uncompressed_size);
      iError := unzCloseCurrentFile(xpiFile);
      Result:= True;
    finally
      iError := unzClose(xpiFile);
      if Assigned(FileBuffer) then
        FreeMem(FileBuffer);
    end;
  end;

//-----------------------------------------
procedure ZeroData;
begin
  foooGenerator:= '';
  foooTitle:= '';
  foooDesc:= '';
  foooSubject:= '';
  foooInitialCreator:= '';
  foooCreator:= '';
  foooCreationDate:= '';
  foooModifDate:= '';
  foooKeywords:= '';
  foooLanguage:= '';
  foooUserInfo1:= '';
  foooUserInfo2:= '';
  foooUserInfo3:= '';
  foooUserInfo4:= '';
  foooURL:= '';
  foooPrintDate:= '';
  foooPrintedBy:= '';
  FillChar(foooEditionTime, SizeOf(foooEditionTime), 0);
  foooEditionCycles:= 0;
  foooNTables:= 0;
  foooNImages:= 0;
  foooNObjects:= 0;
  foooNPages:= 0;
  foooNParags:= 0;
  foooNWords:= 0;
  foooNChars:= 0;
  foooNCells:= 0;
end;

procedure ZeroText;
begin
  foooTextList.Clear;
end;

//-----------------------------------------
procedure GetXmlData(const fn: string);
begin
  ReadXmlFile(fn);
  foooGenerator:=      GetXmlTag('meta:generator');
  foooTitle:=          GetXmlTag('dc:title');
  foooDesc:=    GetXmlTag('dc:description');
  foooSubject:=        GetXmlTag('dc:subject');
  foooInitialCreator:= GetXmlTag('meta:initial-creator');
  foooCreator:=        GetXmlTag('dc:creator');
  foooCreationDate:=   GetXmlTag('meta:creation-date');
  foooModifDate:=      GetXmlTag('dc:date');
  foooKeywords:=       GetXmlTagList('meta:keyword');
  foooLanguage:=       GetXmlTag('dc:language');

  GetXmlUserTags(foooUserInfo1,
                 foooUserInfo2,
                 foooUserInfo3,
                 foooUserInfo4);

  foooURL:=            GetXmlTagParam('meta:auto-reload', 'xlink:href'); 
  foooPrintDate:=      GetXmlTag('meta:print-date');
  foooPrintedBy:=      GetXmlTag('meta:printed-by');
  
  SReplace(foooCreationDate, 'T', ' ');
  SReplace(foooModifDate, 'T', ' ');
  SReplace(foooPrintDate, 'T', ' ');
  
  foooEditionCycles:= StrToIntDef(GetXmlTag('meta:editing-cycles'), 0);
  GetXmlTimeTag('meta:editing-duration', foooEditionTime);
  GetXmlDocStat(foooNTables, foooNImages, foooNObjects, foooNPages, foooNParags, foooNWords, foooNChars, foooNCells);
end;

//-----------------------------------------
var
  fLastFilename: string = '';
  fLastResult: boolean = false;

function GetOOData(const fn: string): boolean;
var
  fn_meta: string;
begin
  if fn=fLastFilename then //file is already read
    begin Result:= fLastResult; Exit end;

  fLastFilename:= fn;
  ZeroData;

  Result:= ExtractFile(PAnsiChar(fn), meta_xml, fn_meta);

  if Result then GetXmlData(fn_meta);

  fLastResult:= Result;
end;

//-----------------------------------------
{
function GetOOText(const fn: string): boolean;
var
  dir, fn_cont: string;
begin
  ZeroText;
  dir:= FGetTempPath;
  Result:= UnzipSingle(fn, dir, content_xml);
  if Result then
    begin
    fn_cont:= dir+'\'+content_xml;
    ReadXmlFile(fn_cont);
    foooText:= GetXmlText;
    DeleteFile(PChar(fn_cont));
    end;
end;
}

//-----------------------------------------
function GetOODataAndText(const fn: string): boolean;
var
  fn_meta, fn_cont: string;
begin
  ZeroData;
  ZeroText;

  Result:= ExtractFile(PAnsiChar(fn), meta_xml, fn_meta);
  Result:= Result and ExtractFile(PAnsiChar(fn), content_xml, fn_cont);

  if Result then
    begin
    if Assigned(XmlProgress) then
      XmlProgress(ssXmlMsgReadingInfo, 0);

    //Get data
    ReadXmlFile(fn_meta);
    foooTitle:= GetXmlTag('dc:title');
    foooDesc:= GetXmlTag('dc:description');
    foooSubject:= GetXmlTag('dc:subject');

    if Assigned(XmlProgress) then
      XmlProgress(ssXmlMsgReadingText, 0);

    //Get text
    ReadXmlFile(fn_cont);
    GetXmlText(foooTextList);
    end;
end;


initialization
   foooTextList:= TStringList.Create;

finalization
   FreeAndNil(foooTextList);

end.
