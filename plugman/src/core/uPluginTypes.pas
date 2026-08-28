unit uPluginTypes;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils;

type
  TPluginType = (ptWCX, ptWDX, ptWFX, ptWLX);

const
  PluginTypeNames: array[TPluginType] of string = ('wcx', 'wdx', 'wfx', 'wlx');
  PluginSectionNames: array[TPluginType] of string =
    ('WcxPlugins', 'WdxPlugins', 'WfxPlugins', 'WlxPlugins');
  PluginNodeNames: array[TPluginType] of string =
    ('WcxPlugin', 'WdxPlugin', 'WfxPlugin', 'WlxPlugin');

  WcxMask = '*.wcx;*.wcx64';
  WdxMask = '*.wdx;*.wdx64';
  WfxMask = '*.wfx;*.wfx64';
  WlxMask = '*.wlx;*.wlx64';

  { WCX capability flags (from WcxPlugin SDK) }
  PK_CAPS_NEW         = 1;
  PK_CAPS_MODIFY      = 2;
  PK_CAPS_MULTIPLE    = 4;
  PK_CAPS_DELETE      = 8;
  PK_CAPS_OPTIONS     = 16;
  PK_CAPS_MEMPACK     = 32;
  PK_CAPS_BY_CONTENT  = 64;
  PK_CAPS_SEARCHTEXT  = 128;
  PK_CAPS_HIDE        = 256;
  PK_CAPS_ENCRYPT     = 512;

  PK_CAP_NAMES: array[0..9] of record
    Flag: Integer;
    Name: string;
  end = (
    (Flag: PK_CAPS_NEW;         Name: 'Create new archives'),
    (Flag: PK_CAPS_MODIFY;      Name: 'Modify existing archives'),
    (Flag: PK_CAPS_MULTIPLE;    Name: 'Multiple files per archive'),
    (Flag: PK_CAPS_DELETE;      Name: 'Delete files'),
    (Flag: PK_CAPS_OPTIONS;     Name: 'Options dialog'),
    (Flag: PK_CAPS_MEMPACK;     Name: 'Pack in memory'),
    (Flag: PK_CAPS_BY_CONTENT;  Name: 'Detect by content'),
    (Flag: PK_CAPS_SEARCHTEXT;  Name: 'Search text in archives'),
    (Flag: PK_CAPS_HIDE;        Name: 'Hide packer icon'),
    (Flag: PK_CAPS_ENCRYPT;     Name: 'Encryption support')
  );

function PluginTypeFromString(const S: string): TPluginType;
function PluginTypeToString(APluginType: TPluginType): string;
function PluginSectionForType(APluginType: TPluginType): string;
function PluginNodeForType(APluginType: TPluginType): string;

implementation

function PluginTypeFromString(const S: string): TPluginType;
var
  L: string;
begin
  L := LowerCase(S);
  if L = 'wcx' then Exit(ptWCX);
  if L = 'wdx' then Exit(ptWDX);
  if L = 'wfx' then Exit(ptWFX);
  if L = 'wlx' then Exit(ptWLX);
  raise Exception.CreateFmt('Unknown plugin type: %s', [S]);
end;

function PluginTypeToString(APluginType: TPluginType): string;
begin
  Result := PluginTypeNames[APluginType];
end;

function PluginSectionForType(APluginType: TPluginType): string;
begin
  Result := PluginSectionNames[APluginType];
end;

function PluginNodeForType(APluginType: TPluginType): string;
begin
  Result := PluginNodeNames[APluginType];
end;

end.
