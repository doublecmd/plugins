unit uPluginTweak;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, uPluginTypes, uDcXml;

procedure ApplyWcxTweak(const Path: string; Extensions: TStrings; Flags: array of Integer);
procedure ApplyListerTweak(APluginType: TPluginType; const Path, Name, DetectString: string);
procedure OpenPluginConfig(const PluginDir: string; const EditorCommand: string);

implementation

uses
  Process, LazFileUtils, uDcPaths, uDcRestart;

type
  TWcxTweakWork = class(TRestartWork)
  private
    FPath: string;
    FExtensions: TStringList;
    FFlags: array of Integer;
  public
    constructor Create(const Path: string; Extensions: TStrings;
      Flags: array of Integer);
    destructor Destroy; override;
    procedure Execute; override;
  end;

  TListerTweakWork = class(TRestartWork)
  private
    FPluginType: TPluginType;
    FPath, FName, FDetectString: string;
  public
    constructor Create(APluginType: TPluginType; const Path, Name, DetectString: string);
    procedure Execute; override;
  end;

constructor TWcxTweakWork.Create(const Path: string; Extensions: TStrings;
  Flags: array of Integer);
var
  I: Integer;
begin
  inherited Create;
  FPath := Path;
  FExtensions := TStringList.Create;
  FExtensions.Assign(Extensions);
  SetLength(FFlags, Length(Flags));
  for I := 0 to High(Flags) do
    FFlags[I] := Flags[I];
end;

destructor TWcxTweakWork.Destroy;
begin
  FExtensions.Free;
  inherited Destroy;
end;

procedure TWcxTweakWork.Execute;
var
  XmlDoc: TDcXmlDocument;
begin
  XmlDoc := TDcXmlDocument.Create;
  try
    XmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    XmlDoc.UpdateWcxExtensions(FPath, FExtensions, FFlags);
    XmlDoc.SaveToFile(DcPaths.DoubleCmdXmlPath);
  finally
    XmlDoc.Free;
  end;
end;

constructor TListerTweakWork.Create(APluginType: TPluginType; const Path, Name,
  DetectString: string);
begin
  inherited Create;
  FPluginType := APluginType;
  FPath := Path;
  FName := Name;
  FDetectString := DetectString;
end;

procedure TListerTweakWork.Execute;
var
  XmlDoc: TDcXmlDocument;
begin
  XmlDoc := TDcXmlDocument.Create;
  try
    XmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    XmlDoc.UpdateDetectString(FPluginType, FPath, FDetectString);
    XmlDoc.UpdateName(FPluginType, FPath, FName);
    XmlDoc.SaveToFile(DcPaths.DoubleCmdXmlPath);
  finally
    XmlDoc.Free;
  end;
end;

procedure ApplyWcxTweak(const Path: string; Extensions: TStrings; Flags: array of Integer);
var
  Work: TWcxTweakWork;
begin
  Work := TWcxTweakWork.Create(Path, Extensions, Flags);
  try
    WithDcRestart(Work);
  finally
    Work.Free;
  end;
end;

procedure ApplyListerTweak(APluginType: TPluginType; const Path, Name, DetectString: string);
var
  Work: TListerTweakWork;
begin
  Work := TListerTweakWork.Create(APluginType, Path, Name, DetectString);
  try
    WithDcRestart(Work);
  finally
    Work.Free;
  end;
end;

procedure OpenPluginConfig(const PluginDir: string; const EditorCommand: string);
var
  SR: TSearchRec;
  ConfigFile, Cmd, Path: string;
  P: TProcess;
  Parts: TStringArray;
begin
  ConfigFile := '';
  Path := IncludeTrailingPathDelimiter(PluginDir);
  if FindFirst(Path + '*', faAnyFile, SR) = 0 then
  try
    repeat
      if (SR.Attr and faDirectory) = 0 then
        if (LowerCase(ExtractFileExt(SR.Name)) = '.ini') or
           (LowerCase(ExtractFileExt(SR.Name)) = '.conf') then
        begin
          ConfigFile := Path + SR.Name;
          Break;
        end;
    until FindNext(SR) <> 0;
  finally
    FindClose(SR);
  end;
  if ConfigFile = '' then
    raise Exception.Create('No .ini or .conf file found in plugin directory.');
  Cmd := EditorCommand;
  if Cmd = '' then Cmd := 'xdg-open';
  Parts := Cmd.Split([' '], 2);
  P := TProcess.Create(nil);
  try
    P.Executable := Parts[0];
    if Length(Parts) > 1 then
      P.Parameters.Add(Parts[1]);
    P.Parameters.Add(ConfigFile);
    P.Options := [poNoConsole];
    P.Execute;
  finally
    P.Free;
  end;
end;

end.
