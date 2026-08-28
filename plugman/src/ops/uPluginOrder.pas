unit uPluginOrder;

{$mode objfpc}{$H+}

interface

uses
  uDcXml;

procedure MovePluginUp(var Entry: TPluginXmlEntry);
procedure MovePluginDown(var Entry: TPluginXmlEntry);

implementation

uses
  uDcPaths, uDcRestart;

type
  TMoveWork = class(TRestartWork)
  private
    FEntry: TPluginXmlEntry;
    FNewIndex: Integer;
  public
    constructor Create(var Entry: TPluginXmlEntry; NewIndex: Integer);
    procedure Execute; override;
    property Entry: TPluginXmlEntry read FEntry write FEntry;
  end;

constructor TMoveWork.Create(var Entry: TPluginXmlEntry; NewIndex: Integer);
begin
  inherited Create;
  FEntry := Entry;
  FNewIndex := NewIndex;
end;

procedure TMoveWork.Execute;
var
  XmlDoc: TDcXmlDocument;
begin
  XmlDoc := TDcXmlDocument.Create;
  try
    XmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    XmlDoc.MoveNode(FEntry, FNewIndex);
    XmlDoc.SaveToFile(DcPaths.DoubleCmdXmlPath);
  finally
    XmlDoc.Free;
  end;
end;

procedure ApplyMove(var Entry: TPluginXmlEntry; NewIndex: Integer);
var
  Work: TMoveWork;
begin
  if Entry.Index = NewIndex then Exit;
  Work := TMoveWork.Create(Entry, NewIndex);
  try
    WithDcRestart(Work);
    Entry := Work.Entry;
  finally
    Work.Free;
  end;
end;

procedure MovePluginUp(var Entry: TPluginXmlEntry);
begin
  if Entry.Index <= 0 then Exit;
  ApplyMove(Entry, Entry.Index - 1);
end;

procedure MovePluginDown(var Entry: TPluginXmlEntry);
begin
  ApplyMove(Entry, Entry.Index + 1);
end;

end.
