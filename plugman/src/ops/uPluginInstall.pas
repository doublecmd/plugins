unit uPluginInstall;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, uPluginTypes, uPlugmanJson, uDcXml, uPluginScanner;

type
  TInstallRequest = record
    ArchivePath: string;
    SourceUrl: string;
    SelectedIndex: Integer;
    WcxExtensions: string;
  end;

function InstallPluginFromArchive(const Request: TInstallRequest): TPluginRecord;
procedure CopyTree(const SourceDir, DestDir: string);

implementation

uses
  LazFileUtils, FileUtil, uDcPaths, uDcRestart, uHttpCheck;

type
  TInstallWork = class(TRestartWork)
  private
    FItem: TScannedPlugin;
    FPluginName: string;
    FDcPath: string;
    FWcxExtensions: string;
  public
    constructor Create(const AItem: TScannedPlugin; const APluginName, ADcPath,
      AWcxExtensions: string);
    procedure Execute; override;
  end;

constructor TInstallWork.Create(const AItem: TScannedPlugin; const APluginName,
  ADcPath, AWcxExtensions: string);
begin
  inherited Create;
  FItem := AItem;
  FPluginName := APluginName;
  FDcPath := ADcPath;
  FWcxExtensions := AWcxExtensions;
end;

procedure TInstallWork.Execute;
var
  XmlDoc: TDcXmlDocument;
  ExtItem: string;
begin
  XmlDoc := TDcXmlDocument.Create;
  try
    XmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    case FItem.PluginType of
      ptWLX:
        XmlDoc.AppendWlx(FPluginName, FDcPath, FItem.SuggestedDetectString, True);
      ptWDX:
        XmlDoc.AppendWdx(FPluginName, FDcPath, FItem.SuggestedDetectString);
      ptWFX:
        XmlDoc.AppendWfx(FPluginName, FDcPath, True);
      ptWCX:
        begin
          if FWcxExtensions = '' then
            XmlDoc.AppendWcx('*', FDcPath, PK_CAPS_NEW or PK_CAPS_MODIFY, True)
          else
            for ExtItem in FWcxExtensions.Split([';', ',']) do
              if Trim(ExtItem) <> '' then
                XmlDoc.AppendWcx(Trim(ExtItem), FDcPath,
                  PK_CAPS_NEW or PK_CAPS_MODIFY, True);
        end;
    end;
    XmlDoc.SaveToFile(DcPaths.DoubleCmdXmlPath);
  finally
    XmlDoc.Free;
  end;
end;

procedure CopyTree(const SourceDir, DestDir: string);

  procedure CopyFiles(const Src, Dst: string);
  var
    SR: TSearchRec;
    SrcPath, DstPath: string;
  begin
    ForceDirectories(Dst);
    if FindFirst(IncludeTrailingPathDelimiter(Src) + '*', faAnyFile, SR) = 0 then
    try
      repeat
        if (SR.Name = '.') or (SR.Name = '..') then Continue;
        SrcPath := IncludeTrailingPathDelimiter(Src) + SR.Name;
        DstPath := IncludeTrailingPathDelimiter(Dst) + SR.Name;
        if (SR.Attr and faDirectory) <> 0 then
          CopyFiles(SrcPath, DstPath)
        else
        begin
          ForceDirectories(ExtractFilePath(DstPath));
          if not CopyFile(SrcPath, DstPath) then
            raise Exception.CreateFmt('Copy failed: %s', [SrcPath]);
        end;
      until FindNext(SR) <> 0;
    finally
      FindClose(SR);
    end;
  end;

begin
  CopyFiles(SourceDir, DestDir);
end;

function InstallPluginFromArchive(const Request: TInstallRequest): TPluginRecord;
var
  ExtractDir: string;
  Scanned: TScannedPluginArray;
  Item: TScannedPlugin;
  PluginDir, RelDir, DcPath, PluginName: string;
  Rec: TPluginRecord;
  Head: THeadResult;
  SelIdx: Integer;
  Work: TInstallWork;
begin
  Scanned := TPluginScanner.ScanArchive(Request.ArchivePath, ExtractDir);
  if Length(Scanned) = 0 then
    raise Exception.Create('No plugin binaries found in archive.');
  SelIdx := Request.SelectedIndex;
  if (SelIdx < 0) or (SelIdx >= Length(Scanned)) then
    SelIdx := 0;
  Item := Scanned[SelIdx];
  if not Item.Valid then
    raise Exception.Create('Plugin binary has incompatible architecture.');
  PluginName := Item.SuggestedName;
  RelDir := PluginTypeToString(Item.PluginType) + PathDelim + PluginName + PathDelim;
  PluginDir := DcPaths.PluginDirForType(Item.PluginType) + PluginName + PathDelim;
  CopyTree(ExtractDir, PluginDir);
  DcPath := DcPaths.MakeDcRelativePath(
    IncludeTrailingPathDelimiter(PluginDir) + ExtractFileName(Item.FilePath));
  Work := TInstallWork.Create(Item, PluginName, DcPath, Request.WcxExtensions);
  try
    WithDcRestart(Work);
  finally
    Work.Free;
  end;
  Rec := PlugmanStore.CreateEntry(PluginName, Item.PluginType,
    ExtractFileName(Item.FilePath), RelDir);
  Rec.SourceUrl := Request.SourceUrl;
  if Request.SourceUrl <> '' then
  begin
    Head := ProbeUrl(Request.SourceUrl);
    Rec.HttpCaps.SupportsHead := Head.SupportsHead;
    Rec.HttpCaps.ProvidesEtag := Head.ETag <> '';
    Rec.HttpCaps.ProvidesModified := Head.LastModified <> '';
    Rec.State.ETag := Head.ETag;
    Rec.State.LastModified := Head.LastModified;
    Rec.State.LocalSize := Head.ContentLength;
    Rec.State.LastChecked := Now;
  end;
  PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
  Result := Rec;
end;

end.
