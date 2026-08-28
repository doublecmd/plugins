unit uDcPaths;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, uPluginTypes;

type
  TDcPaths = class
  private
    FConfigDir: string;
    FCommanderPath: string;
    FCommanderExe: string;
    FEditorCommand: string;
    FAutoCheckUpdates: Boolean;
    procedure DetectPaths;
    function ResolveCommanderExe: string;
  public
    constructor Create;
    procedure LoadSettings(const SettingsFile: string);
    procedure SaveSettings(const SettingsFile: string);
    function DoubleCmdXmlPath: string;
    function PlugmanJsonPath: string;
    function PlugmanSettingsPath: string;
    function PluginsBaseDir: string;
    function PluginDirForType(APluginType: TPluginType): string;
    function ExpandDcPath(const Path: string): string;
    function MakeDcRelativePath(const AbsPath: string): string;
    function MakePluginRelativeDir(const AbsDir: string): string;
    function GetResolvedCommanderExe: string;
    property ConfigDir: string read FConfigDir write FConfigDir;
    property CommanderPath: string read FCommanderPath write FCommanderPath;
    property CommanderExe: string read FCommanderExe write FCommanderExe;
    property EditorCommand: string read FEditorCommand write FEditorCommand;
    property AutoCheckUpdates: Boolean read FAutoCheckUpdates write FAutoCheckUpdates;
  end;

var
  DcPaths: TDcPaths;

implementation

uses
  LazFileUtils, IniFiles, Process;

const
  COMMANDER_PATH_VAR = '%COMMANDER_PATH%';

function GetEnvValue(const Name: string): string;
begin
  Result := GetEnvironmentVariable(Name);
end;

function RunWhich(const ExeName: string): string;
var
  P: TProcess;
  SL: TStringList;
begin
  Result := '';
  P := TProcess.Create(nil);
  SL := TStringList.Create;
  try
    P.Executable := '/usr/bin/which';
    P.Parameters.Add(ExeName);
    P.Options := [poUsePipes, poWaitOnExit, poNoConsole];
    P.ShowWindow := swoHide;
    P.Execute;
    SL.LoadFromStream(P.Output);
    if SL.Count > 0 then
      Result := Trim(SL[0]);
  finally
    SL.Free;
    P.Free;
  end;
end;

function TDcPaths.ResolveCommanderExe: string;
var
  EnvPath: string;
begin
  if FCommanderExe <> '' then
    Exit(ExpandFileName(FCommanderExe));
  EnvPath := RunWhich('doublecmd');
  if EnvPath <> '' then
    Exit(EnvPath);
  Result := '';
end;

procedure TDcPaths.DetectPaths;
var
  XdgConfig: string;
begin
  XdgConfig := GetEnvValue('XDG_CONFIG_HOME');
  if XdgConfig = '' then
    XdgConfig := IncludeTrailingPathDelimiter(GetEnvValue('HOME')) + '.config';
  if FConfigDir = '' then
    FConfigDir := IncludeTrailingPathDelimiter(XdgConfig) + 'doublecmd' + PathDelim;

  if FCommanderPath = '' then
  begin
    FCommanderPath := GetEnvValue('COMMANDER_PATH');
    if FCommanderPath = '' then
    begin
      FCommanderExe := ResolveCommanderExe;
      if FCommanderExe <> '' then
        FCommanderPath := ExcludeTrailingPathDelimiter(ExtractFilePath(FCommanderExe));
    end;
  end;

  if FEditorCommand = '' then
  begin
    FEditorCommand := GetEnvValue('EDITOR');
    if FEditorCommand = '' then
      FEditorCommand := 'xdg-open';
  end;
end;

constructor TDcPaths.Create;
begin
  inherited Create;
  FAutoCheckUpdates := False;
  DetectPaths;
end;

procedure TDcPaths.LoadSettings(const SettingsFile: string);
var
  Ini: TIniFile;
begin
  if not FileExists(SettingsFile) then Exit;
  Ini := TIniFile.Create(SettingsFile);
  try
    FConfigDir := Ini.ReadString('Paths', 'ConfigDir', FConfigDir);
    FCommanderPath := Ini.ReadString('Paths', 'CommanderPath', FCommanderPath);
    FCommanderExe := Ini.ReadString('Paths', 'CommanderExe', FCommanderExe);
    FEditorCommand := Ini.ReadString('General', 'EditorCommand', FEditorCommand);
    FAutoCheckUpdates := Ini.ReadBool('General', 'AutoCheckUpdates', FAutoCheckUpdates);
  finally
    Ini.Free;
  end;
  DetectPaths;
end;

procedure TDcPaths.SaveSettings(const SettingsFile: string);
var
  Ini: TIniFile;
begin
  ForceDirectories(ExtractFilePath(SettingsFile));
  Ini := TIniFile.Create(SettingsFile);
  try
    Ini.WriteString('Paths', 'ConfigDir', FConfigDir);
    Ini.WriteString('Paths', 'CommanderPath', FCommanderPath);
    Ini.WriteString('Paths', 'CommanderExe', FCommanderExe);
    Ini.WriteString('General', 'EditorCommand', FEditorCommand);
    Ini.WriteBool('General', 'AutoCheckUpdates', FAutoCheckUpdates);
  finally
    Ini.Free;
  end;
end;

function TDcPaths.DoubleCmdXmlPath: string;
begin
  Result := IncludeTrailingPathDelimiter(FConfigDir) + 'doublecmd.xml';
end;

function TDcPaths.PlugmanJsonPath: string;
begin
  Result := IncludeTrailingPathDelimiter(FConfigDir) + 'plugman.json';
end;

function TDcPaths.PlugmanSettingsPath: string;
begin
  Result := IncludeTrailingPathDelimiter(FConfigDir) + 'plugman.ini';
end;

function TDcPaths.PluginsBaseDir: string;
begin
  Result := IncludeTrailingPathDelimiter(FCommanderPath) + 'plugins' + PathDelim;
end;

function TDcPaths.PluginDirForType(APluginType: TPluginType): string;
begin
  Result := PluginsBaseDir + PluginTypeToString(APluginType) + PathDelim;
end;

function TDcPaths.ExpandDcPath(const Path: string): string;
var
  S: string;
begin
  S := StringReplace(Path, '$COMMANDER_PATH', FCommanderPath,
    [rfReplaceAll, rfIgnoreCase]);
  S := StringReplace(S, '%COMMANDER_PATH%', FCommanderPath,
    [rfReplaceAll, rfIgnoreCase]);
  Result := ExpandFileName(S);
end;

function TDcPaths.MakeDcRelativePath(const AbsPath: string): string;
var
  NormAbs, NormBase: string;
begin
  NormAbs := ExpandFileName(AbsPath);
  NormBase := ExcludeTrailingPathDelimiter(FCommanderPath);
  if CompareText(Copy(NormAbs, 1, Length(NormBase)), NormBase) = 0 then
    Result := COMMANDER_PATH_VAR + Copy(NormAbs, Length(NormBase) + 1, MaxInt)
  else
    Result := NormAbs;
end;

function TDcPaths.GetResolvedCommanderExe: string;
begin
  Result := ResolveCommanderExe;
end;

function TDcPaths.MakePluginRelativeDir(const AbsDir: string): string;
var
  NormAbs, NormBase: string;
begin
  NormAbs := IncludeTrailingPathDelimiter(ExpandFileName(AbsDir));
  NormBase := PluginsBaseDir;
  if CompareText(Copy(NormAbs, 1, Length(NormBase)), NormBase) = 0 then
    Result := Copy(NormAbs, Length(NormBase) + 1, MaxInt)
  else
    Result := ExtractRelativePath(NormBase, NormAbs);
end;

initialization
  DcPaths := TDcPaths.Create;

finalization
  DcPaths.Free;

end.
