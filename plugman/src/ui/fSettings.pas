unit fSettings;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Dialogs, StdCtrls, ExtCtrls, EditBtn;

type
  TfrmSettings = class(TForm)
    edtConfigDir: TDirectoryEdit;
    edtCommanderPath: TDirectoryEdit;
    edtCommanderExe: TFileNameEdit;
    edtEditor: TEdit;
    chkAutoCheck: TCheckBox;
    lblConfig: TLabel;
    lblCmdPath: TLabel;
    lblCmdExe: TLabel;
    lblEditor: TLabel;
    btnOk: TButton;
    btnCancel: TButton;
    procedure btnOkClick(Sender: TObject);
  public
    function EditSettings: Boolean;
  end;

function ShowSettingsDialog: Boolean;

implementation

{$R *.lfm}

uses uDcPaths;

function ShowSettingsDialog: Boolean;
var
  F: TfrmSettings;
begin
  F := TfrmSettings.Create(nil);
  try
    Result := F.EditSettings;
  finally
    F.Free;
  end;
end;

function TfrmSettings.EditSettings: Boolean;
begin
  edtConfigDir.Text := DcPaths.ConfigDir;
  edtCommanderPath.Text := DcPaths.CommanderPath;
  edtCommanderExe.Text := DcPaths.CommanderExe;
  edtEditor.Text := DcPaths.EditorCommand;
  chkAutoCheck.Checked := DcPaths.AutoCheckUpdates;
  Result := ShowModal = mrOk;
  if Result then
  begin
    DcPaths.ConfigDir := IncludeTrailingPathDelimiter(edtConfigDir.Text);
    DcPaths.CommanderPath := ExcludeTrailingPathDelimiter(edtCommanderPath.Text);
    DcPaths.CommanderExe := edtCommanderExe.Text;
    DcPaths.EditorCommand := edtEditor.Text;
    DcPaths.AutoCheckUpdates := chkAutoCheck.Checked;
    DcPaths.SaveSettings(DcPaths.PlugmanSettingsPath);
  end;
end;

procedure TfrmSettings.btnOkClick(Sender: TObject);
begin
  ModalResult := mrOk;
end;

end.
