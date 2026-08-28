unit fTweakLister;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Dialogs, StdCtrls, uPluginTypes;

type
  TfrmTweakLister = class(TForm)
    lblName: TLabel;
    lblDetect: TLabel;
    edtName: TEdit;
    edtDetect: TEdit;
    btnOk: TButton;
    btnCancel: TButton;
    procedure btnOkClick(Sender: TObject);
  private
    FPluginType: TPluginType;
    FPath: string;
  public
    function EditLister(APluginType: TPluginType; const Path, PluginName, DetectString: string): Boolean;
  end;

function ShowTweakLister(APluginType: TPluginType; const Path, PluginName, DetectString: string): Boolean;

implementation

{$R *.lfm}

uses uPluginTweak;

function ShowTweakLister(APluginType: TPluginType; const Path, PluginName, DetectString: string): Boolean;
var
  F: TfrmTweakLister;
begin
  F := TfrmTweakLister.Create(nil);
  try
    Result := F.EditLister(APluginType, Path, PluginName, DetectString);
  finally
    F.Free;
  end;
end;

function TfrmTweakLister.EditLister(APluginType: TPluginType; const Path, PluginName,
  DetectString: string): Boolean;
begin
  FPluginType := APluginType;
  FPath := Path;
  edtName.Text := PluginName;
  edtDetect.Text := DetectString;
  Result := ShowModal = mrOk;
  if Result then
    ApplyListerTweak(FPluginType, FPath, Trim(edtName.Text), Trim(edtDetect.Text));
end;

procedure TfrmTweakLister.btnOkClick(Sender: TObject);
begin
  if Trim(edtDetect.Text) = '' then
  begin
    ShowMessage('DetectString cannot be empty.');
    Exit;
  end;
  ModalResult := mrOk;
end;

end.
