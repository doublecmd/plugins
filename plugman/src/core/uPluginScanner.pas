unit uPluginScanner;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, uPluginTypes;

type
  TBinaryType = (btUnknown, btElf32, btElf64, btPe32, btPe64);

  TScannedPlugin = record
    PluginType: TPluginType;
    FilePath: string;
    SuggestedName: string;
    SuggestedDetectString: string;
    Valid: Boolean;
  end;

  TScannedPluginArray = array of TScannedPlugin;

  TPluginScanner = class
  public
    class function GetPluginBinaryType(const FileName: string): TBinaryType;
    class function IsValidPluginBinary(const FileName: string): Boolean;
    class function DetectPluginType(const FileName: string): TPluginType;
    class function ExtractArchive(const ArchivePath, DestDir: string): Boolean;
    class function ScanDirectory(const Dir: string): TScannedPluginArray;
    class function ScanArchive(const ArchivePath: string;
      out ExtractDir: string): TScannedPluginArray;
    class function SuggestDetectString(const FileName: string): string;
  end;

implementation

uses
  Process, LazFileUtils;

{$IFDEF CPU64}
const
  TargetBinary: TBinaryType = btElf64;
{$ELSE}
const
  TargetBinary: TBinaryType = btElf32;
{$ENDIF}

class function TPluginScanner.GetPluginBinaryType(const FileName: string): TBinaryType;
var
  FS: TFileStream;
  W: Word;
  D: DWord;
  B: Byte;
begin
  Result := btUnknown;
  if not FileExists(FileName) then Exit;
  FS := TFileStream.Create(FileName, fmOpenRead or fmShareDenyNone);
  try
    if FS.Size < 4 then Exit;
    FS.Read(W, SizeOf(W));
    if W = $5A4D then
    begin
      FS.Seek(60, soBeginning);
      FS.Read(D, SizeOf(D));
      FS.Seek(D, soBeginning);
      FS.Read(D, SizeOf(D));
      if D = $4550 then
      begin
        FS.Seek(20, soCurrent);
        FS.Read(W, SizeOf(W));
        case W of
          $10B: Exit(btPe32);
          $20B: Exit(btPe64);
        end;
      end;
    end;
    FS.Seek(0, soBeginning);
    FS.Read(D, SizeOf(D));
    if D = $464C457F then
    begin
      FS.Read(B, SizeOf(B));
      case B of
        1: Exit(btElf32);
        2: Exit(btElf64);
      end;
    end;
  finally
    FS.Free;
  end;
end;

class function TPluginScanner.IsValidPluginBinary(const FileName: string): Boolean;
begin
  Result := GetPluginBinaryType(FileName) = TargetBinary;
end;

class function TPluginScanner.DetectPluginType(const FileName: string): TPluginType;
var
  Ext: string;
begin
  Ext := LowerCase(ExtractFileExt(FileName));
  if (Ext = '.wcx') or (Ext = '.wcx64') then Exit(ptWCX);
  if (Ext = '.wdx') or (Ext = '.wdx64') then Exit(ptWDX);
  if (Ext = '.wfx') or (Ext = '.wfx64') then Exit(ptWFX);
  if (Ext = '.wlx') or (Ext = '.wlx64') then Exit(ptWLX);
  if (Ext = '.so') then
  begin
    if Pos('wlx', LowerCase(FileName)) > 0 then Exit(ptWLX);
    if Pos('wcx', LowerCase(FileName)) > 0 then Exit(ptWCX);
    if Pos('wdx', LowerCase(FileName)) > 0 then Exit(ptWDX);
    if Pos('wfx', LowerCase(FileName)) > 0 then Exit(ptWFX);
  end;
  raise Exception.CreateFmt('Unknown plugin extension: %s', [FileName]);
end;

class function TPluginScanner.SuggestDetectString(const FileName: string): string;
var
  Base, Ext: string;
begin
  Base := ExtractFileName(FileName);
  Ext := UpperCase(ExtractFileExt(Base));
  if Ext <> '' then
    Delete(Ext, 1, 1);
  if Ext <> '' then
    Result := 'EXT="' + Ext + '"'
  else
    Result := '';
end;

class function TPluginScanner.ExtractArchive(const ArchivePath, DestDir: string): Boolean;
var
  Ext: string;
  P: TProcess;
begin
  Result := False;
  ForceDirectories(DestDir);
  Ext := LowerCase(ExtractFileExt(ArchivePath));
  if Ext = '.zip' then
  begin
    P := TProcess.Create(nil);
    try
      P.Executable := '/usr/bin/unzip';
      P.Parameters.Add('-o');
      P.Parameters.Add(ArchivePath);
      P.Parameters.Add('-d');
      P.Parameters.Add(DestDir);
      P.Options := [poWaitOnExit, poNoConsole];
      P.ShowWindow := swoHide;
      P.Execute;
      Result := P.ExitStatus = 0;
    finally
      P.Free;
    end;
    Exit;
  end;
  if (Ext = '.gz') or (Ext = '.tgz') or (Ext = '.tar') or
     (Copy(LowerCase(ArchivePath), Length(ArchivePath) - 6, 7) = '.tar.gz') then
  begin
    P := TProcess.Create(nil);
    try
      P.Executable := '/bin/tar';
      P.Parameters.Add('-xf');
      P.Parameters.Add(ArchivePath);
      P.Parameters.Add('-C');
      P.Parameters.Add(DestDir);
      P.Options := [poWaitOnExit, poNoConsole];
      P.ShowWindow := swoHide;
      P.Execute;
      Result := P.ExitStatus = 0;
    finally
      P.Free;
    end;
    Exit;
  end;
  raise Exception.CreateFmt('Unsupported archive format: %s', [ArchivePath]);
end;

class function TPluginScanner.ScanDirectory(const Dir: string): TScannedPluginArray;

  procedure ScanFile(const AFile: string);
  var
    Item: TScannedPlugin;
    Pt: TPluginType;
  begin
    try
      Pt := DetectPluginType(AFile);
    except
      Exit;
    end;
    Item.PluginType := Pt;
    Item.FilePath := AFile;
    Item.SuggestedName := ExtractFileNameWithoutExt(AFile);
    Item.SuggestedDetectString := SuggestDetectString(AFile);
    Item.Valid := IsValidPluginBinary(AFile);
    SetLength(Result, Length(Result) + 1);
    Result[High(Result)] := Item;
  end;

  procedure Walk(const BaseDir: string);
  var
    SR: TSearchRec;
    Path: string;
  begin
    if FindFirst(IncludeTrailingPathDelimiter(BaseDir) + '*', faAnyFile, SR) = 0 then
    try
      repeat
        if (SR.Name = '.') or (SR.Name = '..') then Continue;
        Path := IncludeTrailingPathDelimiter(BaseDir) + SR.Name;
        if (SR.Attr and faDirectory) <> 0 then
          Walk(Path)
        else
          ScanFile(Path);
      until FindNext(SR) <> 0;
    finally
      FindClose(SR);
    end;
  end;

begin
  Result := nil;
  Walk(Dir);
end;

class function TPluginScanner.ScanArchive(const ArchivePath: string;
  out ExtractDir: string): TScannedPluginArray;
begin
  ExtractDir := IncludeTrailingPathDelimiter(GetTempDir) +
    'plugman_' + IntToStr(GetProcessID) + PathDelim;
  if not ExtractArchive(ArchivePath, ExtractDir) then
    raise Exception.Create('Failed to extract archive.');
  Result := ScanDirectory(ExtractDir);
end;

end.
