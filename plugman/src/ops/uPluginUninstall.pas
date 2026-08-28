unit uPluginUninstall;

{$mode objfpc}{$H+}

interface

uses
  SysUtils, uPlugmanJson;

procedure UninstallPlugin(ARecord: TPluginRecord);

implementation

uses
  LazFileUtils, FileUtil, uDcPaths, uDcXml, uDcRestart;

type
  TUninstallWork = class(TRestartWork)
  private
    FRecord: TPluginRecord;
  public
    constructor Create(ARecord: TPluginRecord);
    procedure Execute; override;
  end;

constructor TUninstallWork.Create(ARecord: TPluginRecord);
begin
  inherited Create;
  FRecord := ARecord;
end;

procedure TUninstallWork.Execute;
var
  XmlDoc: TDcXmlDocument;
  AbsDir, Prefix: string;
begin
  XmlDoc := TDcXmlDocument.Create;
  try
    XmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    Prefix := DcPaths.ExpandDcPath(
      '%COMMANDER_PATH%/plugins/' + FRecord.RelativeDir);
    XmlDoc.RemoveNodesByPathPrefix(Prefix);
    XmlDoc.SaveToFile(DcPaths.DoubleCmdXmlPath);
  finally
    XmlDoc.Free;
  end;
  AbsDir := DcPaths.PluginsBaseDir + FRecord.RelativeDir;
  if DirectoryExists(AbsDir) then
    DeleteDirectory(AbsDir, True);
  if FRecord.Rollback.BackupExists then
  begin
    AbsDir := DcPaths.PluginsBaseDir + FRecord.RelativeDir +
      FRecord.Rollback.BackupFilename;
    if FileExists(AbsDir) then
      DeleteFile(AbsDir);
  end;
end;

procedure UninstallPlugin(ARecord: TPluginRecord);
var
  Work: TUninstallWork;
begin
  if ARecord = nil then
    raise Exception.Create('No plugin selected.');
  Work := TUninstallWork.Create(ARecord);
  try
    WithDcRestart(Work);
  finally
    Work.Free;
  end;
  PlugmanStore.Remove(ARecord);
  PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
end;

end.
