unit uPluginUpdate;

{$mode objfpc}{$H+}

interface

uses
  SysUtils, uPlugmanJson, uHttpCheck;

function CheckPluginUpdate(ARecord: TPluginRecord; out Head: THeadResult): Boolean;
function ApplyPluginUpdate(ARecord: TPluginRecord): Boolean;

implementation

uses
  LazFileUtils, uDcPaths, uPluginInstall, DateUtils;

function CheckPluginUpdate(ARecord: TPluginRecord; out Head: THeadResult): Boolean;
begin
  Result := False;
  if (ARecord = nil) or (ARecord.SourceUrl = '') then Exit;
  if ARecord.HttpCaps.SupportsHead then
    Head := ProbeUrl(ARecord.SourceUrl)
  else
  begin
    Head.Success := False;
    Head.SupportsHead := False;
  end;
  ARecord.State.LastChecked := Now;
  if Head.Success then
  begin
    ARecord.HttpCaps.ProvidesEtag := Head.ETag <> '';
    ARecord.HttpCaps.ProvidesModified := Head.LastModified <> '';
    Result := HasRemoteChange(ARecord, Head);
  end;
  PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
end;

function ApplyPluginUpdate(ARecord: TPluginRecord): Boolean;
var
  TempArchive, TargetBinary, BackupPath: string;
  Head: THeadResult;
  Req: TInstallRequest;

  procedure CreateBackup;
  begin
    TargetBinary := DcPaths.PluginsBaseDir + ARecord.RelativeDir + ARecord.Filename;
    BackupPath := TargetBinary + '.bkp';
    if FileExists(TargetBinary) then
    begin
      if FileExists(BackupPath) then DeleteFile(BackupPath);
      RenameFile(TargetBinary, BackupPath);
      ARecord.Rollback.BackupExists := True;
      ARecord.Rollback.BackupFilename := ExtractFileName(BackupPath);
      ARecord.Rollback.BackupTimestamp := Now;
    end;
  end;

begin
  Result := False;
  if ARecord.SourceUrl = '' then Exit;
  TempArchive := IncludeTrailingPathDelimiter(GetTempDir) +
    'plugman_dl_' + ARecord.Id + ExtractFileExt(ARecord.SourceUrl);
  if not DownloadUrl(ARecord.SourceUrl, TempArchive, Head) then
    raise Exception.Create('Download failed: ' + Head.ErrorMessage);
  CreateBackup;
  try
    Req.ArchivePath := TempArchive;
    Req.SourceUrl := ARecord.SourceUrl;
    Req.SelectedIndex := 0;
    Req.WcxExtensions := '';
    InstallPluginFromArchive(Req);
    ARecord.State.ETag := Head.ETag;
    ARecord.State.LastModified := Head.LastModified;
    ARecord.State.LocalSize := Head.ContentLength;
    ARecord.State.LastChecked := Now;
    PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
    Result := True;
  except
    if ARecord.Rollback.BackupExists and FileExists(BackupPath) then
    begin
      if FileExists(TargetBinary) then DeleteFile(TargetBinary);
      RenameFile(BackupPath, TargetBinary);
    end;
    raise;
  end;
  if FileExists(TempArchive) then DeleteFile(TempArchive);
end;

end.
