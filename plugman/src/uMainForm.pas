unit uMainForm;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, ComCtrls, ExtCtrls, StdCtrls, Dialogs,
  Menus, uPluginTypes, uPlugmanJson, uDcXml;

type
  TListItemData = class
  public
    JsonRecord: TPluginRecord;
    XmlEntry: TPluginXmlEntry;
    UpdateAvailable: Boolean;
  end;

  { TfrmMain }
  TfrmMain = class(TForm)
    ToolBar: TToolBar;
    btnInstall: TToolButton;
    btnUninstall: TToolButton;
    btnEnable: TToolButton;
    btnMoveUp: TToolButton;
    btnMoveDown: TToolButton;
    btnTweak: TToolButton;
    btnBindUrl: TToolButton;
    btnCheckUpdate: TToolButton;
    btnRollback: TToolButton;
    btnRefresh: TToolButton;
    btnSettings: TToolButton;
    btnConfigure: TToolButton;
    lvPlugins: TListView;
    pnlDetail: TPanel;
    memDetail: TMemo;
    StatusBar: TStatusBar;
    OpenDialog: TOpenDialog;
    procedure FormCreate(Sender: TObject);
    procedure FormShow(Sender: TObject);
    procedure btnInstallClick(Sender: TObject);
    procedure btnUninstallClick(Sender: TObject);
    procedure btnEnableClick(Sender: TObject);
    procedure btnMoveUpClick(Sender: TObject);
    procedure btnMoveDownClick(Sender: TObject);
    procedure btnTweakClick(Sender: TObject);
    procedure btnBindUrlClick(Sender: TObject);
    procedure btnCheckUpdateClick(Sender: TObject);
    procedure btnRollbackClick(Sender: TObject);
    procedure btnRefreshClick(Sender: TObject);
    procedure btnSettingsClick(Sender: TObject);
    procedure btnConfigureClick(Sender: TObject);
    procedure lvPluginsSelectItem(Sender: TObject; Item: TListItem; Selected: Boolean);
  private
    FXmlDoc: TDcXmlDocument;
    procedure InitListColumns;
    procedure ReloadAll;
    procedure RefreshList;
    procedure UpdateDetail;
    procedure UpdateButtons;
    function SelectedData: TListItemData;
    procedure SetStatus(const Msg: string);
  public
  end;

var
  frmMain: TfrmMain;

implementation

{$R *.lfm}

uses
  LazFileUtils, FileUtil, StrUtils, uDcPaths, uPluginInstall, uPluginUninstall, uPluginEnable,
  uPluginOrder, uPluginTweak, uPluginUpdate, uPluginRollback, uHttpCheck,
  uDcRestart, fTweakPacker, fTweakLister, fUrlBind, fUpdateConfirm, fSettings;

procedure TfrmMain.FormCreate(Sender: TObject);
begin
  InitListColumns;
  FXmlDoc := TDcXmlDocument.Create;
  lvPlugins.ReadOnly := True;
  lvPlugins.RowSelect := True;
end;

procedure TfrmMain.FormShow(Sender: TObject);
begin
  DcPaths.LoadSettings(DcPaths.PlugmanSettingsPath);
  if not FileExists(DcPaths.PlugmanJsonPath) then
  begin
    ForceDirectories(DcPaths.ConfigDir);
    PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
  end;
  ReloadAll;
  ReconcileDisabledPlugins;
  if DcPaths.AutoCheckUpdates then
    btnCheckUpdateClick(nil);
end;

procedure TfrmMain.InitListColumns;
begin
  with lvPlugins.Columns.Add do begin Caption := 'Name'; Width := 150; end;
  with lvPlugins.Columns.Add do begin Caption := 'Type'; Width := 80; end;
  with lvPlugins.Columns.Add do begin Caption := 'Status'; Width := 80; end;
  with lvPlugins.Columns.Add do begin Caption := 'Path'; Width := 300; end;
  with lvPlugins.Columns.Add do begin Caption := 'URL'; Width := 200; end;
  with lvPlugins.Columns.Add do begin Caption := 'Update'; Width := 100; end;
  with lvPlugins.Columns.Add do begin Caption := 'Exts/Detect'; Width := 200; end;
end;

procedure TfrmMain.SetStatus(const Msg: string);
begin
  StatusBar.SimpleText := Msg + ' | DC: ' + IfThen(IsDoubleCmdRunning, 'running', 'stopped') +
    ' | Config: ' + DcPaths.ConfigDir;
end;

procedure TfrmMain.ReloadAll;
begin
  PlugmanStore.LoadFromFile(DcPaths.PlugmanJsonPath);
  if FileExists(DcPaths.DoubleCmdXmlPath) then
    FXmlDoc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
  RefreshList;
  SetStatus('Ready');
end;

procedure TfrmMain.RefreshList;
var
  ActiveEntries, DisabledEntries: TPluginXmlEntryArray;
  I: Integer;
  Item: TListItem;
  Data: TListItemData;
  Rec: TPluginRecord;
  Pt: TPluginType;

  procedure AddEntry(const Entry: TPluginXmlEntry);
  var
    J: Integer;
    ExtStr: string;
  begin
    if Entry.PluginType = ptWCX then
      ExtStr := 'EXT="' + UpperCase(Entry.ArchiveExt) + '"'
    else
      ExtStr := Entry.DetectString;

    for J := 0 to lvPlugins.Items.Count - 1 do
    begin
      if SameText(lvPlugins.Items[J].SubItems[2], Entry.Path) then
      begin
        if Entry.PluginType = ptWCX then
        begin
          if lvPlugins.Items[J].SubItems.Count > 5 then
            lvPlugins.Items[J].SubItems[5] := lvPlugins.Items[J].SubItems[5] + ' | ' + ExtStr
          else
            lvPlugins.Items[J].SubItems.Add(ExtStr);
        end;
        Exit;
      end;
    end;

    Item := lvPlugins.Items.Add;
    Data := TListItemData.Create;
    Data.XmlEntry := Entry;
    Data.JsonRecord := PlugmanStore.FindByRelativeDir(
      DcPaths.MakePluginRelativeDir(DcPaths.ExpandDcPath(Entry.Path)));
    if Data.JsonRecord = nil then
      Data.JsonRecord := PlugmanStore.FindByFilename(ExtractFileName(Entry.Path));
    Item.Data := Data;
    Item.Caption := Entry.Name;
    if Item.Caption = '' then Item.Caption := ExtractFileName(Entry.Path);
    Item.SubItems.Add(PluginTypeToString(Entry.PluginType));
    if Entry.IsDisabled then
      Item.SubItems.Add('disabled')
    else if Entry.Enabled then
      Item.SubItems.Add('active')
    else
      Item.SubItems.Add('inactive');
    Item.SubItems.Add(Entry.Path);
    if (Data.JsonRecord <> nil) and (Data.JsonRecord.SourceUrl <> '') then
      Item.SubItems.Add(Data.JsonRecord.SourceUrl)
    else
      Item.SubItems.Add('');
    Item.SubItems.Add('');
    Item.SubItems.Add(ExtStr);
  end;

begin
  lvPlugins.Items.BeginUpdate;
  try
    lvPlugins.Items.Clear;
    for Pt := Low(TPluginType) to High(TPluginType) do
    begin
      ActiveEntries := FXmlDoc.EnumerateActive(Pt);
      for I := 0 to High(ActiveEntries) do
        AddEntry(ActiveEntries[I]);
    end;
    DisabledEntries := FXmlDoc.EnumerateDisabled;
    for I := 0 to High(DisabledEntries) do
      AddEntry(DisabledEntries[I]);
  finally
    lvPlugins.Items.EndUpdate;
  end;
  UpdateButtons;
end;

function TfrmMain.SelectedData: TListItemData;
begin
  Result := nil;
  if (lvPlugins.Selected <> nil) and (lvPlugins.Selected.Data <> nil) then
    Result := TListItemData(lvPlugins.Selected.Data);
end;

procedure TfrmMain.UpdateButtons;
var
  Data: TListItemData;
begin
  Data := SelectedData;
  btnUninstall.Enabled := Data <> nil;
  btnEnable.Enabled := Data <> nil;
  btnMoveUp.Enabled := Data <> nil;
  btnMoveDown.Enabled := Data <> nil;
  btnTweak.Enabled := Data <> nil;
  btnBindUrl.Enabled := Data <> nil;
  btnCheckUpdate.Enabled := (Data <> nil) and (Data.JsonRecord <> nil) and
    (Data.JsonRecord.SourceUrl <> '');
  btnRollback.Enabled := (Data <> nil) and (Data.JsonRecord <> nil) and
    CanRollback(Data.JsonRecord);
  btnConfigure.Enabled := Data <> nil;
  if (Data <> nil) and Data.XmlEntry.IsDisabled then
    btnEnable.Caption := 'Enable'
  else
    btnEnable.Caption := 'Disable';
end;

procedure TfrmMain.UpdateDetail;
var
  Data: TListItemData;
  Rec: TPluginRecord;
  Lines: TStringList;
begin
  Data := SelectedData;
  Lines := TStringList.Create;
  try
    if Data = nil then
    begin
      memDetail.Lines.Text := 'Select a plugin.';
      Exit;
    end;
    Rec := Data.JsonRecord;
    Lines.Add('Name: ' + Data.XmlEntry.Name);
    Lines.Add('Type: ' + PluginTypeToString(Data.XmlEntry.PluginType));
    Lines.Add('Path: ' + Data.XmlEntry.Path);
    if Data.XmlEntry.DetectString <> '' then
      Lines.Add('DetectString: ' + Data.XmlEntry.DetectString);
    if Rec <> nil then
    begin
      Lines.Add('');
      Lines.Add('ID: ' + Rec.Id);
      Lines.Add('Relative dir: ' + Rec.RelativeDir);
      if Rec.SourceUrl <> '' then
      begin
        Lines.Add('URL: ' + Rec.SourceUrl);
        Lines.Add(Format('HEAD supported: %s', [BoolToStr(Rec.HttpCaps.SupportsHead, True)]));
        Lines.Add(Format('ETag: %s', [Rec.State.ETag]));
      end;
      if Rec.Rollback.BackupExists then
        Lines.Add(Format('Backup: %s (%s)', [Rec.Rollback.BackupFilename,
          DateTimeToStr(Rec.Rollback.BackupTimestamp)]));
    end;
    memDetail.Lines.Assign(Lines);
  finally
    Lines.Free;
  end;
end;

procedure TfrmMain.lvPluginsSelectItem(Sender: TObject; Item: TListItem;
  Selected: Boolean);
begin
  UpdateDetail;
  UpdateButtons;
end;

procedure TfrmMain.btnInstallClick(Sender: TObject);
var
  Req: TInstallRequest;
  Url: string;
begin
  OpenDialog.Title := 'Select plugin archive';
  OpenDialog.Filter := 'Archives|*.zip;*.tar.gz;*.tgz;*.tar|All|*.*';
  if not OpenDialog.Execute then Exit;
  if InputQuery('Install', 'Download URL (optional):', Url) then;
  Req.ArchivePath := OpenDialog.FileName;
  Req.SourceUrl := Url;
  Req.SelectedIndex := 0;
  if SameText(ExtractFileExt(OpenDialog.FileName), '.wcx') or
     (Pos('wcx', LowerCase(OpenDialog.FileName)) > 0) then
    InputQuery('WCX extensions', 'Semicolon-separated extensions (e.g. zip;rar):', Req.WcxExtensions);
  try
    InstallPluginFromArchive(Req);
    ReloadAll;
    SetStatus('Plugin installed.');
  except
    on E: Exception do
      ShowMessage('Install failed: ' + E.Message);
  end;
end;

procedure TfrmMain.btnUninstallClick(Sender: TObject);
var
  Data: TListItemData;
begin
  Data := SelectedData;
  if Data = nil then Exit;
  if Data.JsonRecord = nil then
  begin
    ShowMessage('This plugin is not tracked in plugman.json. Add it manually or reinstall.');
    Exit;
  end;
  if MessageDlg('Uninstall', 'Remove plugin "' + Data.XmlEntry.Name +
    '" and delete its files?', mtConfirmation, [mbYes, mbNo], 0) <> mrYes then Exit;
  try
    UninstallPlugin(Data.JsonRecord);
    ReloadAll;
    SetStatus('Plugin uninstalled.');
  except
    on E: Exception do ShowMessage(E.Message);
  end;
end;

procedure TfrmMain.btnEnableClick(Sender: TObject);
var
  Data: TListItemData;
  Entry: TPluginXmlEntry;
begin
  Data := SelectedData;
  if Data = nil then Exit;
  Entry := Data.XmlEntry;
  try
    if Entry.IsDisabled then
      EnablePlugin(Data.JsonRecord, Entry)
    else
      DisablePlugin(Data.JsonRecord, Entry);
    ReloadAll;
    SetStatus('Plugin state updated.');
  except
    on E: Exception do ShowMessage(E.Message);
  end;
end;

procedure TfrmMain.btnMoveUpClick(Sender: TObject);
var
  Data: TListItemData;
  Entry: TPluginXmlEntry;
begin
  Data := SelectedData;
  if Data = nil then Exit;
  Entry := Data.XmlEntry;
  try
    MovePluginUp(Entry);
    ReloadAll;
  except
    on E: Exception do ShowMessage(E.Message);
  end;
end;

procedure TfrmMain.btnMoveDownClick(Sender: TObject);
var
  Data: TListItemData;
  Entry: TPluginXmlEntry;
begin
  Data := SelectedData;
  if Data = nil then Exit;
  Entry := Data.XmlEntry;
  try
    MovePluginDown(Entry);
    ReloadAll;
  except
    on E: Exception do ShowMessage(E.Message);
  end;
end;

procedure TfrmMain.btnConfigureClick(Sender: TObject);
var
  Data: TListItemData;
  PluginDir: string;
begin
  Data := SelectedData;
  if Data = nil then Exit;
  PluginDir := DcPaths.PluginsBaseDir;
  if Data.JsonRecord <> nil then
    PluginDir := PluginDir + Data.JsonRecord.RelativeDir
  else
    PluginDir := ExtractFilePath(DcPaths.ExpandDcPath(Data.XmlEntry.Path));
  try
    OpenPluginConfig(PluginDir, DcPaths.EditorCommand);
  except
    on E: Exception do ShowMessage(E.Message);
  end;
end;

procedure TfrmMain.btnTweakClick(Sender: TObject);
var
  Data: TListItemData;
  PluginDir: string;
begin
  Data := SelectedData;
  if Data = nil then Exit;
  try
    case Data.XmlEntry.PluginType of
      ptWCX:
        ShowTweakPacker(Data.XmlEntry.Path);
      ptWLX, ptWDX:
        ShowTweakLister(Data.XmlEntry.PluginType, Data.XmlEntry.Path,
          Data.XmlEntry.Name, Data.XmlEntry.DetectString);
      ptWFX:
        ShowMessage('Use Configure to edit WFX plugin .ini/.conf files.');
    end;
    ReloadAll;
  except
    on E: Exception do ShowMessage(E.Message);
  end;
end;

procedure TfrmMain.btnBindUrlClick(Sender: TObject);
var
  Data: TListItemData;
  Rec: TPluginRecord;
begin
  Data := SelectedData;
  if Data = nil then Exit;
  Rec := Data.JsonRecord;
  if Rec = nil then
  begin
    Rec := PlugmanStore.CreateEntry(Data.XmlEntry.Name, Data.XmlEntry.PluginType,
      ExtractFileName(Data.XmlEntry.Path),
      DcPaths.MakePluginRelativeDir(ExtractFilePath(DcPaths.ExpandDcPath(Data.XmlEntry.Path))));
    PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
  end;
  if ShowUrlBindDialog(Rec) then
    ReloadAll;
end;

procedure TfrmMain.btnCheckUpdateClick(Sender: TObject);
var
  Data: TListItemData;
  Head: THeadResult;
  I: Integer;
  Item: TListItem;
  D: TListItemData;
begin
  if Sender = btnCheckUpdate then
  begin
    Data := SelectedData;
    if (Data = nil) or (Data.JsonRecord = nil) or (Data.JsonRecord.SourceUrl = '') then Exit;
    try
      if CheckPluginUpdate(Data.JsonRecord, Head) then
      begin
        if not Head.SupportsHead and (Head.ErrorMessage <> '') then
          ShowMessage('Warning: server may not support automated version checking.');
        if ShowUpdateConfirm(Data.JsonRecord.Name, Head) then
        begin
          ApplyPluginUpdate(Data.JsonRecord);
          SetStatus('Update applied.');
        end;
      end
      else
        SetStatus('No update available.');
      ReloadAll;
    except
      on E: Exception do ShowMessage(E.Message);
    end;
  end
  else
  begin
    for I := 0 to lvPlugins.Items.Count - 1 do
    begin
      Item := lvPlugins.Items[I];
      if Item.Data = nil then Continue;
      D := TListItemData(Item.Data);
      if (D.JsonRecord <> nil) and (D.JsonRecord.SourceUrl <> '') then
        CheckPluginUpdate(D.JsonRecord, Head);
    end;
    ReloadAll;
  end;
end;

procedure TfrmMain.btnRollbackClick(Sender: TObject);
var
  Data: TListItemData;
begin
  Data := SelectedData;
  if (Data = nil) or (Data.JsonRecord = nil) then Exit;
  if MessageDlg('Rollback', 'Restore previous version from backup?', mtConfirmation,
    [mbYes, mbNo], 0) <> mrYes then Exit;
  try
    RollbackPlugin(Data.JsonRecord, True);
    ReloadAll;
    SetStatus('Rollback complete.');
  except
    on E: Exception do ShowMessage(E.Message);
  end;
end;

procedure TfrmMain.btnRefreshClick(Sender: TObject);
begin
  ReloadAll;
end;

procedure TfrmMain.btnSettingsClick(Sender: TObject);
begin
  if ShowSettingsDialog then
    ReloadAll;
end;

end.
