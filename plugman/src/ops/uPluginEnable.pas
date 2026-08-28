unit uPluginEnable;

{$mode objfpc}{$H+}

interface

uses
  SysUtils, uPluginTypes, uPlugmanJson, uDcXml;

procedure DisablePlugin(ARecord: TPluginRecord; var Entry: TPluginXmlEntry);
procedure EnablePlugin(ARecord: TPluginRecord; var Entry: TPluginXmlEntry);
procedure ReconcileDisabledPlugins;

implementation

uses
  uDcPaths, uDcRestart;

type
  TDisableWork = class(TRestartWork)
  private
    FRecord: TPluginRecord;
    FEntry: TPluginXmlEntry;
    FSerialized: string;
  public
    constructor Create(ARecord: TPluginRecord; var Entry: TPluginXmlEntry);
    procedure Execute; override;
    property Serialized: string read FSerialized;
    property UpdatedEntry: TPluginXmlEntry read FEntry write FEntry;
  end;

  TEnableWork = class(TRestartWork)
  private
    FRecord: TPluginRecord;
    FEntry: TPluginXmlEntry;
  public
    constructor Create(ARecord: TPluginRecord; var Entry: TPluginXmlEntry);
    procedure Execute; override;
    property UpdatedEntry: TPluginXmlEntry read FEntry write FEntry;
  end;

  TReconcileWork = class(TRestartWork)
  public
    procedure Execute; override;
  end;

constructor TDisableWork.Create(ARecord: TPluginRecord; var Entry: TPluginXmlEntry);
begin
  inherited Create;
  FRecord := ARecord;
  FEntry := Entry;
end;

procedure TDisableWork.Execute;
var
  XmlDoc: TDcXmlDocument;
  Entry: TPluginXmlEntry;
begin
  Entry := FEntry;
  XmlDoc := TDcXmlDocument.Create;
  try
    XmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    if not XmlDoc.EntryValid(Entry) then
    begin
      Entry := XmlDoc.FindByPath(FRecord.PluginType,
        '%COMMANDER_PATH%/plugins/' + FRecord.RelativeDir + FRecord.Filename);
    end;
    if not XmlDoc.EntryValid(Entry) then
      raise Exception.Create('Plugin XML node not found.');
    FSerialized := XmlDoc.SerializeNode(Entry.Node);
    XmlDoc.DisableNode(Entry);
    XmlDoc.SaveToFile(DcPaths.DoubleCmdXmlPath);
    FEntry := Entry;
  finally
    XmlDoc.Free;
  end;
end;

constructor TEnableWork.Create(ARecord: TPluginRecord; var Entry: TPluginXmlEntry);
begin
  inherited Create;
  FRecord := ARecord;
  FEntry := Entry;
end;

procedure TEnableWork.Execute;
var
  XmlDoc: TDcXmlDocument;
  Entry: TPluginXmlEntry;
begin
  Entry := FEntry;
  XmlDoc := TDcXmlDocument.Create;
  try
    XmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    if not XmlDoc.EntryValid(Entry) then
      Entry := XmlDoc.FindDisabledByPath(
        '%COMMANDER_PATH%/plugins/' + FRecord.RelativeDir + FRecord.Filename);
    if not XmlDoc.EntryValid(Entry) then
      raise Exception.Create('Disabled plugin XML node not found.');
    XmlDoc.EnableNode(Entry);
    XmlDoc.SaveToFile(DcPaths.DoubleCmdXmlPath);
    FEntry := Entry;
  finally
    XmlDoc.Free;
  end;
end;

procedure TReconcileWork.Execute;
var
  I: Integer;
  Rec: TPluginRecord;
  XmlDoc: TDcXmlDocument;
  Entry: TPluginXmlEntry;
begin
  XmlDoc := TDcXmlDocument.Create;
  try
    XmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    for I := 0 to PlugmanStore.GetCount - 1 do
    begin
      Rec := PlugmanStore.GetPlugin(I);
      if Rec.ManagerState.Enabled then Continue;
      Entry := XmlDoc.FindDisabledByPath(
        '%COMMANDER_PATH%/plugins/' + Rec.RelativeDir + Rec.Filename);
      if not XmlDoc.EntryValid(Entry) then
      begin
        Entry := XmlDoc.FindByPath(Rec.PluginType,
          '%COMMANDER_PATH%/plugins/' + Rec.RelativeDir + Rec.Filename);
        if XmlDoc.EntryValid(Entry) then
        begin
          XmlDoc.DisableNode(Entry);
          Rec.ManagerState.DisabledNodeXml := XmlDoc.SerializeNode(Entry.Node);
        end
        else if Rec.ManagerState.DisabledNodeXml <> '' then
          XmlDoc.RestoreDisabledFromSerialized(Rec.ManagerState.DisabledNodeXml,
            Rec.ManagerState.DisabledCategory);
      end;
    end;
    XmlDoc.SaveToFile(DcPaths.DoubleCmdXmlPath);
  finally
    XmlDoc.Free;
  end;
end;

procedure DisablePlugin(ARecord: TPluginRecord; var Entry: TPluginXmlEntry);
var
  Work: TDisableWork;
begin
  Work := TDisableWork.Create(ARecord, Entry);
  try
    WithDcRestart(Work);
    Entry := Work.UpdatedEntry;
    if ARecord <> nil then
    begin
      ARecord.ManagerState.Enabled := False;
      ARecord.ManagerState.DisabledCategory :=
        PluginSectionForType(ARecord.PluginType);
      ARecord.ManagerState.DisabledNodeXml := Work.Serialized;
    end;
  finally
    Work.Free;
  end;
  if ARecord <> nil then
    PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
end;

procedure EnablePlugin(ARecord: TPluginRecord; var Entry: TPluginXmlEntry);
var
  Work: TEnableWork;
begin
  Work := TEnableWork.Create(ARecord, Entry);
  try
    WithDcRestart(Work);
    Entry := Work.UpdatedEntry;
    if ARecord <> nil then
    begin
      ARecord.ManagerState.Enabled := True;
      ARecord.ManagerState.DisabledCategory := '';
      ARecord.ManagerState.DisabledNodeXml := '';
    end;
  finally
    Work.Free;
  end;
  if ARecord <> nil then
    PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
end;

procedure ReconcileDisabledPlugins;
var
  Work: TReconcileWork;
begin
  if PlugmanStore.GetCount = 0 then Exit;
  if not FileExists(DcPaths.DoubleCmdXmlPath) then Exit;
  Work := TReconcileWork.Create;
  try
    WithDcRestart(Work, False);
  finally
    Work.Free;
  end;
end;

end.
