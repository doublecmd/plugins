unit fTweakPacker;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Dialogs, StdCtrls, ExtCtrls, Grids,
  uPluginTypes, uDcXml;

type
  TfrmTweakPacker = class(TForm)
    edtExts: TEdit;
    pnlFlags: TPanel;
    lblFlags: TLabel;
    btnOk: TButton;
    btnCancel: TButton;
    procedure FormCreate(Sender: TObject);
    procedure btnOkClick(Sender: TObject);
  private
    FPath: string;
    FCheckBoxes: array[0..9] of TCheckBox;
    procedure BuildFlagCheckboxes;
    procedure LoadFromXml;
    function CurrentFlags: Integer;
  public
    function EditWcx(const Path: string): Boolean;
  end;

function ShowTweakPacker(const Path: string): Boolean;

implementation

{$R *.lfm}

uses uPluginTweak, uDcPaths;

function ShowTweakPacker(const Path: string): Boolean;
var
  F: TfrmTweakPacker;
begin
  F := TfrmTweakPacker.Create(nil);
  try
    Result := F.EditWcx(Path);
  finally
    F.Free;
  end;
end;

procedure TfrmTweakPacker.FormCreate(Sender: TObject);
begin
  BuildFlagCheckboxes;
end;

procedure TfrmTweakPacker.BuildFlagCheckboxes;
var
  I, TopPos: Integer;
begin
  TopPos := 24;
  for I := 0 to High(PK_CAP_NAMES) do
  begin
    FCheckBoxes[I] := TCheckBox.Create(Self);
    FCheckBoxes[I].Parent := pnlFlags;
    FCheckBoxes[I].Left := 8;
    FCheckBoxes[I].Top := TopPos;
    FCheckBoxes[I].Width := 400;
    FCheckBoxes[I].Caption := PK_CAP_NAMES[I].Name;
    FCheckBoxes[I].Tag := PK_CAP_NAMES[I].Flag;
    Inc(TopPos, 24);
  end;
end;

procedure TfrmTweakPacker.LoadFromXml;
var
  Doc: TDcXmlDocument;
  Entries: TPluginXmlEntryArray;
  I, J, Flags: Integer;
  ExtStr: string;
begin
  Doc := TDcXmlDocument.Create;
  ExtStr := '';
  try
    Doc.LoadFromFile(DcPaths.DoubleCmdXmlPath);
    Entries := Doc.EnumerateActive(ptWCX);
    for I := 0 to High(Entries) do
      if SameText(DcPaths.ExpandDcPath(Entries[I].Path), DcPaths.ExpandDcPath(FPath)) then
      begin
        if ExtStr <> '' then
          ExtStr := ExtStr + ' | ';
        ExtStr := ExtStr + 'EXT="' + UpperCase(Entries[I].ArchiveExt) + '"';
        
        Flags := Entries[I].Flags;
        for J := 0 to High(FCheckBoxes) do
          FCheckBoxes[J].Checked := (Flags and FCheckBoxes[J].Tag) <> 0;
      end;
    edtExts.Text := ExtStr;
  finally
    Doc.Free;
  end;
end;

function TfrmTweakPacker.CurrentFlags: Integer;
var
  I: Integer;
begin
  Result := 0;
  for I := 0 to High(FCheckBoxes) do
    if FCheckBoxes[I].Checked then
      Result := Result or FCheckBoxes[I].Tag;
end;

function TfrmTweakPacker.EditWcx(const Path: string): Boolean;
var
  Exts: TStringList;
  FlagsArr: array of Integer;
  R, I: Integer;
  Parts: TStringArray;
  S, ExtValue: string;
begin
  FPath := Path;
  LoadFromXml;
  Result := ShowModal = mrOk;
  if Result then
  begin
    Exts := TStringList.Create;
    try
      S := edtExts.Text;
      Parts := S.Split(['|']);
      for I := 0 to High(Parts) do
      begin
        S := Trim(Parts[I]);
        if S = '' then Continue;
        if (Pos('EXT="', S) = 1) and (S[Length(S)] = '"') then
          ExtValue := Copy(S, 6, Length(S) - 6)
        else
          ExtValue := S;
        Exts.Add(ExtValue);
      end;
      
      SetLength(FlagsArr, Exts.Count);
      for R := 0 to Exts.Count - 1 do
        FlagsArr[R] := CurrentFlags;
        
      ApplyWcxTweak(FPath, Exts, FlagsArr);
    finally
      Exts.Free;
    end;
  end;
end;

procedure TfrmTweakPacker.btnOkClick(Sender: TObject);
begin
  ModalResult := mrOk;
end;

end.
