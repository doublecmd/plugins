program test_core;

{$mode objfpc}{$H+}

uses
  SysUtils, uPluginTypes, uDcPaths, uPlugmanJson, uDcXml, uPluginScanner;

begin
  DcPaths.ConfigDir := ExpandFileName('../testdata/config/');
  DcPaths.CommanderPath := ExpandFileName('../testdata/commander');
  DcPaths.LoadSettings(DcPaths.ConfigDir + 'plugman.ini');

  if not FileExists(DcPaths.DoubleCmdXmlPath) then
    raise Exception.Create('doublecmd.xml missing');

  PlugmanStore.LoadFromFile(DcPaths.PlugmanJsonPath);
  if PlugmanStore.SchemaVersion <> 1 then
    raise Exception.Create('schema version mismatch');

  with TDcXmlDocument.Create do
  try
    LoadFromFile(DcPaths.DoubleCmdXmlPath);
    if Length(EnumerateActive(ptWLX)) = 0 then
      raise Exception.Create('expected WLX plugin in XML');
  finally
    Free;
  end;

  WriteLn('Core integration checks passed.');
end.
