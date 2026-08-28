program plugman;

{$mode objfpc}{$H+}

uses
  {$IFDEF UNIX}{$IFDEF UseCThreads}
  cthreads,
  {$ENDIF}{$ENDIF}
  Interfaces,
  Forms,
  uMainForm;

begin
  RequireDerivedFormResource := True;
  Application.Scaled := True;
  Application.Title := 'Double Commander Plugin Manager';
  Application.Initialize;
  Application.CreateForm(TfrmMain, frmMain);
  Application.Run;
end.
