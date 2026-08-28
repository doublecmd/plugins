unit fUrlBind;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Dialogs, StdCtrls, ExtCtrls, uPlugmanJson, uHttpCheck;

type
  TfrmUrlBind = class(TForm)
    edtUrl: TEdit;
    lblUrl: TLabel;
    lblStatus: TLabel;
    btnOk: TButton;
    btnCancel: TButton;
    btnProbe: TButton;
    procedure btnOkClick(Sender: TObject);
    procedure btnProbeClick(Sender: TObject);
  public
    function EditUrl(ARecord: TPluginRecord): Boolean;
  end;

function ShowUrlBindDialog(ARecord: TPluginRecord): Boolean;

implementation

{$R *.lfm}

uses uDcPaths;

function ShowUrlBindDialog(ARecord: TPluginRecord): Boolean;
var
  F: TfrmUrlBind;
begin
  F := TfrmUrlBind.Create(nil);
  try
    Result := F.EditUrl(ARecord);
  finally
    F.Free;
  end;
end;

function TfrmUrlBind.EditUrl(ARecord: TPluginRecord): Boolean;
var
  Head: THeadResult;
begin
  edtUrl.Text := ARecord.SourceUrl;
  lblStatus.Caption := '';
  Result := ShowModal = mrOk;
  if Result then
  begin
    ARecord.SourceUrl := Trim(edtUrl.Text);
    if ARecord.SourceUrl <> '' then
    begin
      Head := ProbeUrl(ARecord.SourceUrl);
      ARecord.HttpCaps.SupportsHead := Head.SupportsHead;
      ARecord.HttpCaps.ProvidesEtag := Head.ETag <> '';
      ARecord.HttpCaps.ProvidesModified := Head.LastModified <> '';
      ARecord.State.ETag := Head.ETag;
      ARecord.State.LastModified := Head.LastModified;
      ARecord.State.LocalSize := Head.ContentLength;
      ARecord.State.LastChecked := Now;
    end;
    PlugmanStore.SaveToFile(DcPaths.PlugmanJsonPath);
  end;
end;

procedure TfrmUrlBind.btnOkClick(Sender: TObject);
begin
  ModalResult := mrOk;
end;

procedure TfrmUrlBind.btnProbeClick(Sender: TObject);
var
  Head: THeadResult;
begin
  Head := ProbeUrl(Trim(edtUrl.Text));
  if Head.Success then
    lblStatus.Caption := Format('HEAD OK. ETag=%s Size=%d', [Head.ETag, Head.ContentLength])
  else if not Head.SupportsHead then
    lblStatus.Caption := 'Warning: server rejects HEAD. Full download required for updates.'
  else
    lblStatus.Caption := 'Probe failed: ' + Head.ErrorMessage;
end;

end.
