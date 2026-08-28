unit uPlugmanJson;

{$mode objfpc}{$H+}
{$modeswitch advancedrecords}

interface

uses
  Classes, SysUtils, Contnrs, fpjson, jsonparser, uPluginTypes;

type
  THttpCapabilities = record
    SupportsHead: Boolean;
    ProvidesEtag: Boolean;
    ProvidesModified: Boolean;
    procedure Clear;
    procedure LoadFromJson(AObj: TJSONObject);
    procedure SaveToJson(AObj: TJSONObject);
  end;

  TStateTracking = record
    ETag: string;
    LastModified: string;
    LocalSize: Int64;
    LastChecked: TDateTime;
    procedure Clear;
    procedure LoadFromJson(AObj: TJSONObject);
    procedure SaveToJson(AObj: TJSONObject);
  end;

  TRollbackState = record
    BackupExists: Boolean;
    BackupFilename: string;
    BackupTimestamp: TDateTime;
    procedure Clear;
    procedure LoadFromJson(AObj: TJSONObject);
    procedure SaveToJson(AObj: TJSONObject);
  end;

  TManagerState = record
    Enabled: Boolean;
    DisabledCategory: string;
    DisabledNodeXml: string;
    procedure Clear;
    procedure LoadFromJson(AObj: TJSONObject);
    procedure SaveToJson(AObj: TJSONObject);
  end;

  TPluginRecord = class
  public
    Id: string;
    Name: string;
    PluginType: TPluginType;
    Filename: string;
    RelativeDir: string;
    SourceUrl: string;
    HttpCaps: THttpCapabilities;
    State: TStateTracking;
    Rollback: TRollbackState;
    ManagerState: TManagerState;
    procedure LoadFromJson(AObj: TJSONObject);
    procedure SaveToJson(AObj: TJSONObject);
  end;

  TPlugmanStore = class
  private
    FSchemaVersion: Integer;
    FLastUpdated: TDateTime;
    FPlugins: TObjectList;
  public
    constructor Create;
    destructor Destroy; override;
    procedure Clear;
    procedure LoadFromFile(const Path: string);
    procedure SaveToFile(const Path: string);
    function FindById(const Id: string): TPluginRecord;
    function FindByRelativeDir(const Dir: string): TPluginRecord;
    function FindByFilename(const AName: string): TPluginRecord;
    function CreateEntry(const AName: string; APluginType: TPluginType;
      const AFilename, ARelativeDir: string): TPluginRecord;
    function Add(ARecord: TPluginRecord): Integer;
    procedure Remove(ARecord: TPluginRecord);
    function GetPlugin(Index: Integer): TPluginRecord;
    function GetCount: Integer;
    property SchemaVersion: Integer read FSchemaVersion write FSchemaVersion;
    property LastUpdated: TDateTime read FLastUpdated write FLastUpdated;
  end;

function NewGuidString: string;
function DateTimeToIso8601Utc(ADateTime: TDateTime): string;
function Iso8601ToDateTime(const S: string): TDateTime;

var
  PlugmanStore: TPlugmanStore;

implementation

uses
  DateUtils, uuid;

function NewGuidString: string;
var
  G: TGUID;
begin
  CreateGUID(G);
  Result := LowerCase(GUIDToString(G));
  Result := StringReplace(Result, '{', '', [rfReplaceAll]);
  Result := StringReplace(Result, '}', '', [rfReplaceAll]);
end;

function DateTimeToIso8601Utc(ADateTime: TDateTime): string;
begin
  Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss"Z"', ADateTime);
end;

function Iso8601ToDateTime(const S: string): TDateTime;
var
  T: string;
begin
  if S = '' then
    Exit(0);
  T := S;
  if (Length(T) > 0) and (T[Length(T)] = 'Z') then
    Delete(T, Length(T), 1);
  Result := ISO8601ToDate(T, False);
  if Result = 0 then
    Result := StrToDateTimeDef(S, 0);
end;

{ THttpCapabilities }

procedure THttpCapabilities.Clear;
begin
  SupportsHead := True;
  ProvidesEtag := False;
  ProvidesModified := False;
end;

procedure THttpCapabilities.LoadFromJson(AObj: TJSONObject);
begin
  Clear;
  if AObj = nil then Exit;
  SupportsHead := AObj.Get('supports_head', True);
  ProvidesEtag := AObj.Get('provides_etag', False);
  ProvidesModified := AObj.Get('provides_modified', False);
end;

procedure THttpCapabilities.SaveToJson(AObj: TJSONObject);
begin
  AObj.Add('supports_head', SupportsHead);
  AObj.Add('provides_etag', ProvidesEtag);
  AObj.Add('provides_modified', ProvidesModified);
end;

{ TStateTracking }

procedure TStateTracking.Clear;
begin
  ETag := '';
  LastModified := '';
  LocalSize := 0;
  LastChecked := 0;
end;

procedure TStateTracking.LoadFromJson(AObj: TJSONObject);
begin
  Clear;
  if AObj = nil then Exit;
  ETag := AObj.Get('etag', '');
  LastModified := AObj.Get('last_modified', '');
  LocalSize := AObj.Get('local_size', Int64(0));
  LastChecked := Iso8601ToDateTime(AObj.Get('last_checked', ''));
end;

procedure TStateTracking.SaveToJson(AObj: TJSONObject);
begin
  AObj.Add('etag', ETag);
  AObj.Add('last_modified', LastModified);
  AObj.Add('local_size', LocalSize);
  if LastChecked <> 0 then
    AObj.Add('last_checked', DateTimeToIso8601Utc(LastChecked));
end;

{ TRollbackState }

procedure TRollbackState.Clear;
begin
  BackupExists := False;
  BackupFilename := '';
  BackupTimestamp := 0;
end;

procedure TRollbackState.LoadFromJson(AObj: TJSONObject);
begin
  Clear;
  if AObj = nil then Exit;
  BackupExists := AObj.Get('backup_exists', False);
  BackupFilename := AObj.Get('backup_filename', '');
  BackupTimestamp := Iso8601ToDateTime(AObj.Get('backup_timestamp', ''));
end;

procedure TRollbackState.SaveToJson(AObj: TJSONObject);
begin
  AObj.Add('backup_exists', BackupExists);
  AObj.Add('backup_filename', BackupFilename);
  if BackupTimestamp <> 0 then
    AObj.Add('backup_timestamp', DateTimeToIso8601Utc(BackupTimestamp));
end;

{ TManagerState }

procedure TManagerState.Clear;
begin
  Enabled := True;
  DisabledCategory := '';
  DisabledNodeXml := '';
end;

procedure TManagerState.LoadFromJson(AObj: TJSONObject);
begin
  Clear;
  if AObj = nil then Exit;
  Enabled := AObj.Get('enabled', True);
  DisabledCategory := AObj.Get('disabled_category', '');
  DisabledNodeXml := AObj.Get('disabled_node_xml', '');
end;

procedure TManagerState.SaveToJson(AObj: TJSONObject);
begin
  AObj.Add('enabled', Enabled);
  if DisabledCategory <> '' then
    AObj.Add('disabled_category', DisabledCategory);
  if DisabledNodeXml <> '' then
    AObj.Add('disabled_node_xml', DisabledNodeXml);
end;

{ TPluginRecord }

procedure TPluginRecord.LoadFromJson(AObj: TJSONObject);
var
  SrcObj, StateObj, RollbackObj, MgrObj: TJSONObject;
begin
  Id := AObj.Get('id', '');
  Name := AObj.Get('name', '');
  PluginType := PluginTypeFromString(AObj.Get('type', 'wlx'));
  Filename := AObj.Get('filename', '');
  RelativeDir := AObj.Get('relative_dir', '');
  SourceUrl := '';
  HttpCaps.Clear;
  State.Clear;
  Rollback.Clear;
  ManagerState.Clear;
  SrcObj := AObj.Find('source') as TJSONObject;
  if SrcObj <> nil then
  begin
    SourceUrl := SrcObj.Get('url', '');
    HttpCaps.LoadFromJson(SrcObj.Find('http_capabilities') as TJSONObject);
  end;
  StateObj := AObj.Find('state_tracking') as TJSONObject;
  if StateObj <> nil then
    State.LoadFromJson(StateObj);
  RollbackObj := AObj.Find('rollback') as TJSONObject;
  if RollbackObj <> nil then
    Rollback.LoadFromJson(RollbackObj);
  MgrObj := AObj.Find('manager_state') as TJSONObject;
  if MgrObj <> nil then
    ManagerState.LoadFromJson(MgrObj);
end;

procedure TPluginRecord.SaveToJson(AObj: TJSONObject);
var
  SrcObj, CapsObj, StateObj, RollbackObj, MgrObj: TJSONObject;
begin
  AObj.Add('id', Id);
  AObj.Add('name', Name);
  AObj.Add('type', PluginTypeToString(PluginType));
  AObj.Add('filename', Filename);
  AObj.Add('relative_dir', RelativeDir);
  SrcObj := TJSONObject.Create;
  SrcObj.Add('url', SourceUrl);
  CapsObj := TJSONObject.Create;
  HttpCaps.SaveToJson(CapsObj);
  SrcObj.Add('http_capabilities', CapsObj);
  AObj.Add('source', SrcObj);
  StateObj := TJSONObject.Create;
  State.SaveToJson(StateObj);
  AObj.Add('state_tracking', StateObj);
  RollbackObj := TJSONObject.Create;
  Rollback.SaveToJson(RollbackObj);
  AObj.Add('rollback', RollbackObj);
  if not ManagerState.Enabled or (ManagerState.DisabledNodeXml <> '') then
  begin
    MgrObj := TJSONObject.Create;
    ManagerState.SaveToJson(MgrObj);
    AObj.Add('manager_state', MgrObj);
  end;
end;

{ TPlugmanStore }

constructor TPlugmanStore.Create;
begin
  inherited Create;
  FSchemaVersion := 1;
  FPlugins := TObjectList.Create(True);
end;

destructor TPlugmanStore.Destroy;
begin
  FPlugins.Free;
  inherited Destroy;
end;

procedure TPlugmanStore.Clear;
begin
  FPlugins.Clear;
  FSchemaVersion := 1;
  FLastUpdated := 0;
end;

procedure TPlugmanStore.LoadFromFile(const Path: string);
var
  Parser: TJSONParser;
  Root, Arr, Item: TJSONData;
  I: Integer;
  Rec: TPluginRecord;
  JsonText: string;
begin
  Clear;
  if not FileExists(Path) then Exit;
  with TStringList.Create do
  try
    LoadFromFile(Path);
    JsonText := Text;
  finally
    Free;
  end;
  Parser := TJSONParser.Create(JsonText);
  try
    Root := Parser.Parse;
    if not (Root is TJSONObject) then Exit;
    FSchemaVersion := TJSONObject(Root).Get('schema_version', 1);
    FLastUpdated := Iso8601ToDateTime(TJSONObject(Root).Get('last_updated', ''));
    Arr := TJSONObject(Root).Find('plugins');
    if Arr is TJSONArray then
      for I := 0 to TJSONArray(Arr).Count - 1 do
      begin
        Item := TJSONArray(Arr).Items[I];
        if Item is TJSONObject then
        begin
          Rec := TPluginRecord.Create;
          Rec.LoadFromJson(TJSONObject(Item));
          FPlugins.Add(Rec);
        end;
      end;
  finally
    Parser.Free;
  end;
end;

procedure TPlugmanStore.SaveToFile(const Path: string);
var
  Root, Arr: TJSONData;
  I: Integer;
  ItemObj: TJSONObject;
  Text: string;
  TmpPath: string;
begin
  FLastUpdated := Now;
  Root := TJSONObject.Create;
  try
    TJSONObject(Root).Add('schema_version', FSchemaVersion);
    TJSONObject(Root).Add('last_updated', DateTimeToIso8601Utc(FLastUpdated));
    Arr := TJSONArray.Create;
    for I := 0 to FPlugins.Count - 1 do
    begin
      ItemObj := TJSONObject.Create;
      GetPlugin(I).SaveToJson(ItemObj);
      TJSONArray(Arr).Add(ItemObj);
    end;
    TJSONObject(Root).Add('plugins', Arr);
    Text := Root.AsJSON;
  finally
    Root.Free;
  end;
  ForceDirectories(ExtractFilePath(Path));
  TmpPath := Path + '.tmp';
  with TStringList.Create do
  try
    Text := Text;
    Add(Text);
    SaveToFile(TmpPath);
  finally
    Free;
  end;
  if FileExists(Path) then
    DeleteFile(Path);
  RenameFile(TmpPath, Path);
end;

function TPlugmanStore.FindById(const Id: string): TPluginRecord;
var
  I: Integer;
begin
  Result := nil;
  for I := 0 to FPlugins.Count - 1 do
    if SameText(GetPlugin(I).Id, Id) then
      Exit(GetPlugin(I));
end;

function TPlugmanStore.FindByRelativeDir(const Dir: string): TPluginRecord;
var
  I: Integer;
  D: string;
begin
  Result := nil;
  D := IncludeTrailingPathDelimiter(Dir);
  for I := 0 to FPlugins.Count - 1 do
    if SameText(IncludeTrailingPathDelimiter(GetPlugin(I).RelativeDir), D) then
      Exit(GetPlugin(I));
end;

function TPlugmanStore.FindByFilename(const AName: string): TPluginRecord;
var
  I: Integer;
begin
  Result := nil;
  for I := 0 to FPlugins.Count - 1 do
    if SameText(GetPlugin(I).Filename, AName) then
      Exit(GetPlugin(I));
end;

function TPlugmanStore.CreateEntry(const AName: string; APluginType: TPluginType;
  const AFilename, ARelativeDir: string): TPluginRecord;
begin
  Result := TPluginRecord.Create;
  Result.Id := NewGuidString;
  Result.Name := AName;
  Result.PluginType := APluginType;
  Result.Filename := AFilename;
  Result.RelativeDir := IncludeTrailingPathDelimiter(ARelativeDir);
  Result.HttpCaps.Clear;
  Result.State.Clear;
  Result.Rollback.Clear;
  Result.ManagerState.Clear;
  FPlugins.Add(Result);
end;

function TPlugmanStore.Add(ARecord: TPluginRecord): Integer;
begin
  Result := FPlugins.Add(ARecord);
end;

procedure TPlugmanStore.Remove(ARecord: TPluginRecord);
begin
  FPlugins.Remove(ARecord);
end;

function TPlugmanStore.GetPlugin(Index: Integer): TPluginRecord;
begin
  Result := TPluginRecord(FPlugins[Index]);
end;

function TPlugmanStore.GetCount: Integer;
begin
  Result := FPlugins.Count;
end;

initialization
  PlugmanStore := TPlugmanStore.Create;

finalization
  PlugmanStore.Free;

end.
