unit uPluginRollback;

{$mode objfpc}{$H+}

interface

uses
  SysUtils, uPlugmanJson;

function CanRollback(ARecord: TPluginRecord): Boolean;
procedure RollbackPlugin(ARecord: TPluginRecord; ClearBackupInfo: Boolean);

implementation

uses
  FileUtil, uDcPaths, uDcRestart;

type
  TRollbackWork = class(TRestartWork)
  private
    FRecord: TPluginRecord;
    FClearBackupInfo: Boolean;
  public
    constructor Create(ARecord: TPluginRecord; ClearBackupInfo: Boolean);
    procedure Execute; override;
  end;

constructor TRollbackWork.Create(ARecord: TPluginRecord; ClearBackupInfo: Boolean);
begin
  inherited Create;
  FRecord := ARecord;
  FClearBackupInfo := ClearBackupInfo;
end;

procedure TRollbackWork.Execute;
var
  TargetBinary, BackupPath: string;
begin
  TargetBinary := DcPaths.PluginsBaseDir + FRecord.RelativeDir + FRecord.Filename;
  BackupPath := DcPaths.PluginsBaseDir + FRecord.RelativeDir +
    FRecord.Rollback.BackupFilename;
  if not FileExists(BackupPath) then
    raise Exception.Create('Backup file not found.');
  if FileExists(TargetBinary) then DeleteFile(TargetBinary);
  if not CopyFile(BackupPath, TargetBinary) then
    raise Exception.Create('Failed to restore backup file.');
  if FClearBackupInfo then
  begin
    FRecord.Rollback.Clear;
    PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
  end;
end;

function CanRollback(ARecord: TPluginRecord): Boolean;
var
  BackupPath: string;
begin
  Result := False;
  if (ARecord = nil) or not ARecord.Rollback.BackupExists then Exit;
  BackupPath := DcPaths.PluginsBaseDir + ARecord.RelativeDir +
    ARecord.Rollback.BackupFilename;
  Result := FileExists(BackupPath);
end;

procedure RollbackPlugin(ARecord: TPluginRecord; ClearBackupInfo: Boolean);
var
  Work: TRollbackWork;
begin
  if not CanRollback(ARecord) then
    raise Exception.Create('No backup available for rollback.');
  Work := TRollbackWork.Create(ARecord, ClearBackupInfo);
  try
    WithDcRestart(Work);
  finally
    Work.Free;
  end;
end;

end.
