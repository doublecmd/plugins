unit uDcRestart;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils;

type
  TRestartWork = class
  public
    procedure Execute; virtual; abstract;
  end;

procedure WithDcRestart(Work: TRestartWork; RelaunchAfter: Boolean = True);
function IsDoubleCmdRunning: Boolean;
function GetDoubleCmdPid: Integer;
function TerminateDoubleCmd: Boolean;
procedure LaunchDoubleCmd;

implementation

uses
  Process, uDcPaths;

function RunPgrep: string;
var
  P: TProcess;
  SL: TStringList;
begin
  Result := '';
  P := TProcess.Create(nil);
  SL := TStringList.Create;
  try
    P.Executable := '/usr/bin/pgrep';
    P.Parameters.Add('-x');
    P.Parameters.Add('doublecmd');
    P.Options := [poUsePipes, poWaitOnExit, poNoConsole];
    P.ShowWindow := swoHide;
    P.Execute;
    if P.ExitStatus = 0 then
    begin
      SL.LoadFromStream(P.Output);
      if SL.Count > 0 then
        Result := Trim(SL[0]);
    end;
  finally
    SL.Free;
    P.Free;
  end;
end;

function IsDoubleCmdRunning: Boolean;
begin
  Result := RunPgrep <> '';
end;

function GetDoubleCmdPid: Integer;
begin
  Result := StrToIntDef(RunPgrep, 0);
end;

function SendSignal(Pid: Integer; Sig: Integer): Boolean;
var
  P: TProcess;
begin
  Result := False;
  if Pid <= 0 then Exit;
  P := TProcess.Create(nil);
  try
    P.Executable := '/bin/kill';
    P.Parameters.Add('-' + IntToStr(Sig));
    P.Parameters.Add(IntToStr(Pid));
    P.Options := [poWaitOnExit, poNoConsole];
    P.ShowWindow := swoHide;
    P.Execute;
    Result := P.ExitStatus = 0;
  finally
    P.Free;
  end;
end;

function TerminateDoubleCmd: Boolean;
var
  Pid, WaitMs: Integer;
begin
  Result := True;
  Pid := GetDoubleCmdPid;
  if Pid <= 0 then Exit;
  SendSignal(Pid, 15);
  WaitMs := 0;
  while IsDoubleCmdRunning and (WaitMs < 10000) do
  begin
    Sleep(200);
    Inc(WaitMs, 200);
  end;
  if IsDoubleCmdRunning then
  begin
    Pid := GetDoubleCmdPid;
    SendSignal(Pid, 9);
    Sleep(500);
  end;
  Result := not IsDoubleCmdRunning;
end;

procedure LaunchDoubleCmd;
var
  P: TProcess;
  Exe: string;
begin
  Exe := DcPaths.CommanderExe;
  if Exe = '' then
    Exe := DcPaths.GetResolvedCommanderExe;
  if Exe = '' then Exit;
  P := TProcess.Create(nil);
  try
    P.Executable := Exe;
    P.Options := [poNoConsole];
    P.Execute;
  finally
    P.Free;
  end;
end;

procedure WithDcRestart(Work: TRestartWork; RelaunchAfter: Boolean);
var
  WasRunning: Boolean;
begin
  WasRunning := IsDoubleCmdRunning;
  if WasRunning then
  begin
    if not TerminateDoubleCmd then
      raise Exception.Create('Double Commander did not terminate. Operation aborted.');
  end;
  try
    Work.Execute;
  except
    on E: Exception do
    begin
      if WasRunning and RelaunchAfter then
        LaunchDoubleCmd;
      raise;
    end;
  end;
  if WasRunning and RelaunchAfter then
    LaunchDoubleCmd;
end;

end.
