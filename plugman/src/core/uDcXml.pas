unit uDcXml;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, DOM, XMLRead, XMLWrite, uPluginTypes;

type
  TPluginXmlEntry = record
    PluginType: TPluginType;
    Node: TDOMNode;
    ParentNode: TDOMNode;
    Index: Integer;
    Enabled: Boolean;
    Name: string;
    Path: string;
    DetectString: string;
    ArchiveExt: string;
    Flags: Integer;
    IsDisabled: Boolean;
    OriginalCategory: string;
  end;

  TPluginXmlEntryArray = array of TPluginXmlEntry;

  TDcXmlDocument = class
  private
    FDoc: TXMLDocument;
    FPluginsNode: TDOMNode;
    function GetOrCreatePluginsRoot: TDOMNode;
    function GetOrCreateSection(APluginType: TPluginType): TDOMNode;
    function GetOrCreateDisabledSection: TDOMNode;
    function FindChildValue(ANode: TDOMNode; const AName: string): string;
    function FindChildInt(ANode: TDOMNode; const AName: string; ADefault: Integer): Integer;
    procedure SetChildValue(ANode: TDOMNode; const AName, AValue: string);
    procedure SetChildInt(ANode: TDOMNode; const AName: string; AValue: Integer);
    function GetAttrBool(ANode: TDOMNode; const AName: string; ADefault: Boolean): Boolean;
    procedure SetAttrBool(ANode: TDOMNode; const AName: string; AValue: Boolean);
    procedure FillEntry(var Entry: TPluginXmlEntry; ANode: TDOMNode;
      APluginType: TPluginType; AParent: TDOMNode; AIndex: Integer;
      IsDisabled: Boolean);
  public
    constructor Create;
    destructor Destroy; override;
    procedure LoadFromFile(const Path: string);
    procedure SaveToFile(const Path: string);
    function EnumerateActive(APluginType: TPluginType): TPluginXmlEntryArray;
    function EnumerateDisabled: TPluginXmlEntryArray;
    function EnumerateAllActive: TPluginXmlEntryArray;
    function FindByPath(APluginType: TPluginType; const Path: string): TPluginXmlEntry;
    function FindDisabledByPath(const Path: string): TPluginXmlEntry;
    function EntryValid(const Entry: TPluginXmlEntry): Boolean;
    procedure AppendWlx(const Name, Path, DetectString: string; Enabled: Boolean);
    procedure AppendWcx(const ArchiveExt, Path: string; Flags: Integer; Enabled: Boolean);
    procedure AppendWdx(const Name, Path, DetectString: string);
    procedure AppendWfx(const Name, Path: string; Enabled: Boolean);
    procedure RemoveNode(var Entry: TPluginXmlEntry);
    procedure MoveNode(var Entry: TPluginXmlEntry; NewIndex: Integer);
    procedure DisableNode(var Entry: TPluginXmlEntry);
    procedure EnableNode(var Entry: TPluginXmlEntry);
    function SerializeNode(ANode: TDOMNode): string;
    procedure RestoreDisabledFromSerialized(const SerializedXml, OriginalCategory: string);
    procedure RemoveNodesByPathPrefix(const PathPrefix: string);
    procedure UpdateWcxFlags(const Path: string; ArchiveExt: string; Flags: Integer);
    procedure UpdateWcxExtensions(const Path: string; Extensions: TStrings; Flags: array of Integer);
    procedure UpdateDetectString(APluginType: TPluginType; const Path, DetectString: string);
    procedure UpdateName(APluginType: TPluginType; const Path, Name: string);
    property Document: TXMLDocument read FDoc;
  end;

function LoadDcXml(const Path: string): TDcXmlDocument;

implementation

uses
  uDcPaths;

function NodeIndexAmongSameTag(ANode: TDOMNode): Integer;
var
  Cur: TDOMNode;
begin
  Result := 0;
  Cur := ANode.PreviousSibling;
  while Cur <> nil do
  begin
    if (Cur.NodeType = ELEMENT_NODE) and
       SameText(Cur.NodeName, ANode.NodeName) then
      Inc(Result);
    Cur := Cur.PreviousSibling;
  end;
end;

function LoadDcXml(const Path: string): TDcXmlDocument;
begin
  Result := TDcXmlDocument.Create;
  Result.LoadFromFile(Path);
end;

constructor TDcXmlDocument.Create;
begin
  inherited Create;
  FDoc := TXMLDocument.Create;
end;

destructor TDcXmlDocument.Destroy;
begin
  FDoc.Free;
  inherited Destroy;
end;

function TDcXmlDocument.GetOrCreatePluginsRoot: TDOMNode;
var
  Root: TDOMNode;
begin
  Root := FDoc.DocumentElement;
  if Root = nil then
  begin
    Root := FDoc.CreateElement('DoubleCommander');
    FDoc.AppendChild(Root);
  end;
  FPluginsNode := Root.FindNode('Plugins');
  if FPluginsNode = nil then
  begin
    FPluginsNode := FDoc.CreateElement('Plugins');
    Root.AppendChild(FPluginsNode);
  end;
  Result := FPluginsNode;
end;

function TDcXmlDocument.GetOrCreateSection(APluginType: TPluginType): TDOMNode;
var
  SecName: string;
begin
  GetOrCreatePluginsRoot;
  SecName := PluginSectionForType(APluginType);
  Result := FPluginsNode.FindNode(SecName);
  if Result = nil then
  begin
    Result := FDoc.CreateElement(SecName);
    FPluginsNode.AppendChild(Result);
  end;
end;

function TDcXmlDocument.GetOrCreateDisabledSection: TDOMNode;
begin
  GetOrCreatePluginsRoot;
  Result := FPluginsNode.FindNode('DisabledPlugins');
  if Result = nil then
  begin
    Result := FDoc.CreateElement('DisabledPlugins');
    FPluginsNode.AppendChild(Result);
  end;
end;

function TDcXmlDocument.FindChildValue(ANode: TDOMNode; const AName: string): string;
var
  N: TDOMNode;
begin
  Result := '';
  N := ANode.FindNode(AName);
  if (N <> nil) and (N.FirstChild <> nil) then
    Result := N.TextContent;
end;

function TDcXmlDocument.FindChildInt(ANode: TDOMNode; const AName: string;
  ADefault: Integer): Integer;
var
  S: string;
begin
  S := FindChildValue(ANode, AName);
  if S = '' then
    Exit(ADefault);
  Result := StrToIntDef(S, ADefault);
end;

procedure TDcXmlDocument.SetChildValue(ANode: TDOMNode; const AName, AValue: string);
var
  N: TDOMNode;
begin
  N := ANode.FindNode(AName);
  if N = nil then
  begin
    N := FDoc.CreateElement(AName);
    ANode.AppendChild(N);
  end;
  N.TextContent := AValue;
end;

procedure TDcXmlDocument.SetChildInt(ANode: TDOMNode; const AName: string; AValue: Integer);
begin
  SetChildValue(ANode, AName, IntToStr(AValue));
end;

function TDcXmlDocument.GetAttrBool(ANode: TDOMNode; const AName: string;
  ADefault: Boolean): Boolean;
var
  Attr: TDOMNode;
begin
  if ANode.Attributes <> nil then
  begin
    Attr := ANode.Attributes.GetNamedItem(AName);
    if Attr <> nil then
      Exit(SameText(Attr.NodeValue, 'true') or (Attr.NodeValue = '1'));
  end;
  Result := ADefault;
end;

procedure TDcXmlDocument.SetAttrBool(ANode: TDOMNode; const AName: string; AValue: Boolean);
begin
  if not (ANode is TDOMElement) then Exit;
  if AValue then
    TDOMElement(ANode).SetAttribute(AName, 'True')
  else
    TDOMElement(ANode).SetAttribute(AName, 'False');
end;

procedure TDcXmlDocument.FillEntry(var Entry: TPluginXmlEntry; ANode: TDOMNode;
  APluginType: TPluginType; AParent: TDOMNode; AIndex: Integer; IsDisabled: Boolean);
begin
  Entry.PluginType := APluginType;
  Entry.Node := ANode;
  Entry.ParentNode := AParent;
  Entry.Index := AIndex;
  Entry.IsDisabled := IsDisabled;
  Entry.OriginalCategory := '';
  Entry.Name := '';
  Entry.Path := '';
  Entry.DetectString := '';
  Entry.ArchiveExt := '';
  Entry.Flags := 0;
  Entry.Enabled := True;
  if IsDisabled then
  begin
    if ANode is TDOMElement then
      Entry.OriginalCategory := TDOMElement(ANode).GetAttribute('OriginalCategory');
  end;
  case APluginType of
    ptWLX:
      begin
        Entry.Enabled := GetAttrBool(ANode, 'Enabled', True);
        Entry.Name := FindChildValue(ANode, 'Name');
        Entry.Path := FindChildValue(ANode, 'Path');
        Entry.DetectString := FindChildValue(ANode, 'DetectString');
      end;
    ptWCX:
      begin
        Entry.Enabled := GetAttrBool(ANode, 'Enabled', True);
        Entry.ArchiveExt := FindChildValue(ANode, 'ArchiveExt');
        Entry.Path := FindChildValue(ANode, 'Path');
        Entry.Flags := FindChildInt(ANode, 'Flags', 0);
        Entry.Name := ExtractFileName(Entry.Path);
      end;
    ptWDX:
      begin
        Entry.Name := FindChildValue(ANode, 'Name');
        Entry.Path := FindChildValue(ANode, 'Path');
        Entry.DetectString := FindChildValue(ANode, 'DetectString');
      end;
    ptWFX:
      begin
        Entry.Enabled := GetAttrBool(ANode, 'Enabled', True);
        Entry.Name := FindChildValue(ANode, 'Name');
        Entry.Path := FindChildValue(ANode, 'Path');
      end;
  end;
end;

procedure TDcXmlDocument.LoadFromFile(const Path: string);
begin
  if FileExists(Path) then
    ReadXMLFile(FDoc, Path)
  else
  begin
    FDoc.Free;
    FDoc := TXMLDocument.Create;
    GetOrCreatePluginsRoot;
  end;
end;

procedure TDcXmlDocument.SaveToFile(const Path: string);
var
  TmpPath: string;
begin
  ForceDirectories(ExtractFilePath(Path));
  TmpPath := Path + '.tmp';
  WriteXMLFile(FDoc, TmpPath);
  if FileExists(Path) then
    DeleteFile(Path);
  RenameFile(TmpPath, Path);
end;

function TDcXmlDocument.EnumerateActive(APluginType: TPluginType): TPluginXmlEntryArray;
var
  Sec, N: TDOMNode;
  Entry: TPluginXmlEntry;
  Idx: Integer;
begin
  Result := nil;
  GetOrCreatePluginsRoot;
  Sec := FPluginsNode.FindNode(PluginSectionForType(APluginType));
  if Sec = nil then Exit;
  N := Sec.FirstChild;
  Idx := 0;
  while N <> nil do
  begin
    if (N.NodeType = ELEMENT_NODE) and
       SameText(N.NodeName, PluginNodeForType(APluginType)) then
    begin
      FillEntry(Entry, N, APluginType, Sec, Idx, False);
      SetLength(Result, Length(Result) + 1);
      Result[High(Result)] := Entry;
      Inc(Idx);
    end;
    N := N.NextSibling;
  end;
end;

function TDcXmlDocument.EnumerateDisabled: TPluginXmlEntryArray;
var
  Sec, N: TDOMNode;
  Entry: TPluginXmlEntry;
  Cat: string;
  Pt: TPluginType;
  Idx: Integer;
begin
  Result := nil;
  GetOrCreatePluginsRoot;
  Sec := FPluginsNode.FindNode('DisabledPlugins');
  if Sec = nil then Exit;
  N := Sec.FirstChild;
  Idx := 0;
  while N <> nil do
  begin
    if N.NodeType = ELEMENT_NODE then
    begin
      Cat := '';
      if N is TDOMElement then
        Cat := TDOMElement(N).GetAttribute('OriginalCategory');
      for Pt := Low(TPluginType) to High(TPluginType) do
        if SameText(Cat, PluginSectionForType(Pt)) or
           SameText(N.NodeName, PluginNodeForType(Pt)) then
        begin
          FillEntry(Entry, N, Pt, Sec, Idx, True);
          if Entry.OriginalCategory = '' then
            Entry.OriginalCategory := Cat;
          SetLength(Result, Length(Result) + 1);
          Result[High(Result)] := Entry;
          Inc(Idx);
          Break;
        end;
    end;
    N := N.NextSibling;
  end;
end;

function TDcXmlDocument.EnumerateAllActive: TPluginXmlEntryArray;
var
  Pt: TPluginType;
  Part: TPluginXmlEntryArray;
  I: Integer;
begin
  Result := nil;
  for Pt := Low(TPluginType) to High(TPluginType) do
  begin
    Part := EnumerateActive(Pt);
    for I := 0 to High(Part) do
    begin
      SetLength(Result, Length(Result) + 1);
      Result[High(Result)] := Part[I];
    end;
  end;
end;

function TDcXmlDocument.EntryValid(const Entry: TPluginXmlEntry): Boolean;
begin
  Result := Entry.Node <> nil;
end;

function TDcXmlDocument.FindByPath(APluginType: TPluginType; const Path: string): TPluginXmlEntry;
var
  Entries: TPluginXmlEntryArray;
  I: Integer;
  Expanded, EPath: string;
begin
  FillChar(Result, SizeOf(Result), 0);
  Expanded := DcPaths.ExpandDcPath(Path);
  Entries := EnumerateActive(APluginType);
  for I := 0 to High(Entries) do
  begin
    EPath := DcPaths.ExpandDcPath(Entries[I].Path);
    if SameText(EPath, Expanded) or SameText(Entries[I].Path, Path) then
      Exit(Entries[I]);
  end;
end;

function TDcXmlDocument.FindDisabledByPath(const Path: string): TPluginXmlEntry;
var
  Entries: TPluginXmlEntryArray;
  I: Integer;
  Expanded, EPath: string;
begin
  FillChar(Result, SizeOf(Result), 0);
  Expanded := DcPaths.ExpandDcPath(Path);
  Entries := EnumerateDisabled;
  for I := 0 to High(Entries) do
  begin
    EPath := DcPaths.ExpandDcPath(Entries[I].Path);
    if SameText(EPath, Expanded) or SameText(Entries[I].Path, Path) then
      Exit(Entries[I]);
  end;
end;

procedure TDcXmlDocument.AppendWlx(const Name, Path, DetectString: string; Enabled: Boolean);
var
  Sec, N: TDOMNode;
begin
  Sec := GetOrCreateSection(ptWLX);
  N := FDoc.CreateElement('WlxPlugin');
  SetAttrBool(N, 'Enabled', Enabled);
  SetChildValue(N, 'Name', Name);
  SetChildValue(N, 'Path', Path);
  SetChildValue(N, 'DetectString', DetectString);
  Sec.AppendChild(N);
end;

procedure TDcXmlDocument.AppendWcx(const ArchiveExt, Path: string; Flags: Integer;
  Enabled: Boolean);
var
  Sec, N: TDOMNode;
begin
  Sec := GetOrCreateSection(ptWCX);
  N := FDoc.CreateElement('WcxPlugin');
  SetAttrBool(N, 'Enabled', Enabled);
  SetChildValue(N, 'ArchiveExt', ArchiveExt);
  SetChildValue(N, 'Path', Path);
  SetChildInt(N, 'Flags', Flags);
  Sec.AppendChild(N);
end;

procedure TDcXmlDocument.AppendWdx(const Name, Path, DetectString: string);
var
  Sec, N: TDOMNode;
begin
  Sec := GetOrCreateSection(ptWDX);
  N := FDoc.CreateElement('WdxPlugin');
  SetChildValue(N, 'Name', Name);
  SetChildValue(N, 'Path', Path);
  SetChildValue(N, 'DetectString', DetectString);
  Sec.AppendChild(N);
end;

procedure TDcXmlDocument.AppendWfx(const Name, Path: string; Enabled: Boolean);
var
  Sec, N: TDOMNode;
begin
  Sec := GetOrCreateSection(ptWFX);
  N := FDoc.CreateElement('WfxPlugin');
  SetAttrBool(N, 'Enabled', Enabled);
  SetChildValue(N, 'Name', Name);
  SetChildValue(N, 'Path', Path);
  Sec.AppendChild(N);
end;

procedure TDcXmlDocument.RemoveNode(var Entry: TPluginXmlEntry);
begin
  if Entry.Node = nil then Exit;
  Entry.ParentNode.RemoveChild(Entry.Node);
  Entry.Node := nil;
end;

procedure TDcXmlDocument.MoveNode(var Entry: TPluginXmlEntry; NewIndex: Integer);
var
  Sec, Ref: TDOMNode;
  CurIdx: Integer;
begin
  if Entry.Node = nil then Exit;
  Sec := Entry.ParentNode;
  CurIdx := NodeIndexAmongSameTag(Entry.Node);
  if CurIdx = NewIndex then Exit;
  Sec.RemoveChild(Entry.Node);
  Ref := Sec.FirstChild;
  while (Ref <> nil) and (NewIndex > 0) do
  begin
    if (Ref.NodeType = ELEMENT_NODE) and
       SameText(Ref.NodeName, Entry.Node.NodeName) then
    begin
      Dec(NewIndex);
      if NewIndex = 0 then Break;
    end;
    Ref := Ref.NextSibling;
  end;
  if Ref <> nil then
    Sec.InsertBefore(Entry.Node, Ref)
  else
    Sec.AppendChild(Entry.Node);
  Entry.Index := NodeIndexAmongSameTag(Entry.Node);
end;

function TDcXmlDocument.SerializeNode(ANode: TDOMNode): string;
var
  Doc: TXMLDocument;
  SS: TStringStream;
begin
  Result := '';
  if ANode = nil then Exit;
  Doc := TXMLDocument.Create;
  SS := TStringStream.Create('');
  try
    Doc.AppendChild(Doc.ImportNode(ANode, True));
    WriteXMLFile(Doc, SS);
    Result := SS.DataString;
  finally
    SS.Free;
    Doc.Free;
  end;
end;

procedure TDcXmlDocument.DisableNode(var Entry: TPluginXmlEntry);
var
  DisabledSec, Clone: TDOMNode;
  XmlText: string;
begin
  if Entry.Node = nil then Exit;
  DisabledSec := GetOrCreateDisabledSection;
  Clone := Entry.Node.CloneNode(True);
  if Clone is TDOMElement then
    TDOMElement(Clone).SetAttribute('OriginalCategory', PluginSectionForType(Entry.PluginType));
  DisabledSec.AppendChild(Clone);
  RemoveNode(Entry);
  Entry.IsDisabled := True;
  Entry.OriginalCategory := PluginSectionForType(Entry.PluginType);
  XmlText := SerializeNode(Clone);
  Entry.Node := Clone;
  Entry.ParentNode := DisabledSec;
end;

procedure TDcXmlDocument.EnableNode(var Entry: TPluginXmlEntry);
var
  ActiveSec, Clone: TDOMNode;
  Cat: string;
  Pt: TPluginType;
begin
  if Entry.Node = nil then Exit;
  Cat := Entry.OriginalCategory;
  if Cat = '' then
  begin
    if Entry.Node is TDOMElement then
      Cat := TDOMElement(Entry.Node).GetAttribute('OriginalCategory');
  end;
  Pt := Entry.PluginType;
  if Cat <> '' then
  begin
    for Pt := Low(TPluginType) to High(TPluginType) do
      if SameText(Cat, PluginSectionForType(Pt)) then Break;
  end;
  ActiveSec := GetOrCreateSection(Pt);
  Clone := Entry.Node.CloneNode(True);
  if Clone is TDOMElement then
    TDOMElement(Clone).RemoveAttribute('OriginalCategory');
  ActiveSec.AppendChild(Clone);
  RemoveNode(Entry);
  Entry.IsDisabled := False;
  Entry.Node := Clone;
  Entry.ParentNode := ActiveSec;
  Entry.PluginType := Pt;
end;

procedure TDcXmlDocument.RestoreDisabledFromSerialized(const SerializedXml,
  OriginalCategory: string);
var
  Parser: TXMLDocument;
  N, DisabledSec: TDOMNode;
  SS: TStringStream;
begin
  if SerializedXml = '' then Exit;
  Parser := TXMLDocument.Create;
  SS := TStringStream.Create(SerializedXml);
  try
    ReadXMLFragment(Parser, SS);
    if Parser.DocumentElement <> nil then
    begin
      DisabledSec := GetOrCreateDisabledSection;
      N := Parser.DocumentElement.CloneNode(True);
      if N is TDOMElement then
        TDOMElement(N).SetAttribute('OriginalCategory', OriginalCategory);
      DisabledSec.AppendChild(N);
    end;
  finally
    SS.Free;
    Parser.Free;
  end;
end;

procedure TDcXmlDocument.RemoveNodesByPathPrefix(const PathPrefix: string);
var
  All: TPluginXmlEntryArray;
  I: Integer;
  EPath, Prefix: string;
  E: TPluginXmlEntry;
begin
  Prefix := DcPaths.ExpandDcPath(PathPrefix);
  All := EnumerateAllActive;
  for I := 0 to High(All) do
  begin
    EPath := DcPaths.ExpandDcPath(All[I].Path);
    if (Pos(Prefix, EPath) = 1) or SameText(EPath, Prefix) then
    begin
      E := All[I];
      RemoveNode(E);
    end;
  end;
  All := EnumerateDisabled;
  for I := 0 to High(All) do
  begin
    EPath := DcPaths.ExpandDcPath(All[I].Path);
    if Pos(Prefix, EPath) = 1 then
    begin
      E := All[I];
      RemoveNode(E);
    end;
  end;
end;

procedure TDcXmlDocument.UpdateWcxFlags(const Path: string; ArchiveExt: string;
  Flags: Integer);
var
  Entries: TPluginXmlEntryArray;
  I: Integer;
begin
  Entries := EnumerateActive(ptWCX);
  for I := 0 to High(Entries) do
    if SameText(DcPaths.ExpandDcPath(Entries[I].Path), DcPaths.ExpandDcPath(Path)) and
       SameText(Entries[I].ArchiveExt, ArchiveExt) then
      SetChildInt(Entries[I].Node, 'Flags', Flags);
end;

procedure TDcXmlDocument.UpdateWcxExtensions(const Path: string; Extensions: TStrings; Flags: array of Integer);
var
  Entries: TPluginXmlEntryArray;
  I, J: Integer;
  Sec: TDOMNode;
  Found: Boolean;
  ExpandedTarget: string;
  ExtStr: string;
begin
  ExpandedTarget := DcPaths.ExpandDcPath(Path);
  Entries := EnumerateActive(ptWCX);
  Sec := GetOrCreateSection(ptWCX);
  
  for I := 0 to High(Entries) do
  begin
    if SameText(DcPaths.ExpandDcPath(Entries[I].Path), ExpandedTarget) then
    begin
      Found := False;
      for J := 0 to Extensions.Count - 1 do
      begin
        if SameText(Entries[I].ArchiveExt, Extensions[J]) then
        begin
          SetChildInt(Entries[I].Node, 'Flags', Flags[J]);
          Found := True;
          Extensions[J] := ''; 
          Break;
        end;
      end;
      
      if not Found then
        RemoveNode(Entries[I]);
    end;
  end;

  for J := 0 to Extensions.Count - 1 do
  begin
    ExtStr := Trim(Extensions[J]);
    if ExtStr <> '' then
      AppendWcx(ExtStr, Path, Flags[J], True);
  end;
end;

procedure TDcXmlDocument.UpdateDetectString(APluginType: TPluginType;
  const Path, DetectString: string);
var
  Entries: TPluginXmlEntryArray;
  I: Integer;
begin
  Entries := EnumerateActive(APluginType);
  for I := 0 to High(Entries) do
    if SameText(DcPaths.ExpandDcPath(Entries[I].Path), DcPaths.ExpandDcPath(Path)) then
      SetChildValue(Entries[I].Node, 'DetectString', DetectString);
end;

procedure TDcXmlDocument.UpdateName(APluginType: TPluginType; const Path, Name: string);
var
  Entries: TPluginXmlEntryArray;
  I: Integer;
begin
  Entries := EnumerateActive(APluginType);
  for I := 0 to High(Entries) do
    if SameText(DcPaths.ExpandDcPath(Entries[I].Path), DcPaths.ExpandDcPath(Path)) then
      SetChildValue(Entries[I].Node, 'Name', Name);
end;

end.
