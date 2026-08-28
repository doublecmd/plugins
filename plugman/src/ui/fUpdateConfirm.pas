unit fUpdateConfirm;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Dialogs, StdCtrls, ExtCtrls, uHttpCheck;

type
  TfrmUpdateConfirm = class(TForm)
    pnlMain: TPanel;
    lblDisclaimer: TLabel;
    lblDetails: TLabel;
    pnlButtons: TPanel;
    btnOk: TButton;
    btnCancel: TButton;
    procedure btnOkClick(Sender: TObject);
    procedure btnCancelClick(Sender: TObject);
  public
    function ConfirmUpdate(const PluginName: string; const Head: THeadResult): Boolean;
  end;

function ShowUpdateConfirm(const PluginName: string; const Head: THeadResult): Boolean;

implementation

{$R *.lfm}

function ShowUpdateConfirm(const PluginName: string; const Head: THeadResult): Boolean;
var
  F: TfrmUpdateConfirm;
begin
  F := TfrmUpdateConfirm.Create(nil);
  try
    Result := F.ConfirmUpdate(PluginName, Head);
  finally
    F.Free;
  end;
end;

function TfrmUpdateConfirm.ConfirmUpdate(const PluginName: string;
  const Head: THeadResult): Boolean;
begin
  Caption := 'Update ' + PluginName;
  lblDisclaimer.Caption := UPDATE_DISCLAIMER;
  lblDetails.Caption := Format('Remote ETag: %s'#10'Last-Modified: %s'#10'Size: %d',
    [Head.ETag, Head.LastModified, Head.ContentLength]);
  Result := ShowModal = mrOk;
end;

procedure TfrmUpdateConfirm.btnOkClick(Sender: TObject);
begin
  ModalResult := mrOk;
end;

procedure TfrmUpdateConfirm.btnCancelClick(Sender: TObject);
begin
  ModalResult := mrCancel;
end;

end.
