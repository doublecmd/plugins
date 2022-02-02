{
   Double commander
   -------------------------------------------------------------------------
   xpi_wdx is destined to obtainin the information from xpi-files
   in which extensions and themes for Gecko-based applications are distributed.

   Copyright (C) 2010-2019 Alexander Koblov (alexx2000@mail.ru)

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as 
   published by the Free Software Foundation, either version 3 of the 
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
}

unit xpi_wdx_intf;

{$mode objfpc}{$H+}
{$include calling.inc}

interface

uses
  Classes, SysUtils, WdxPlugin;

procedure ContentGetDetectString(DetectString: PAnsiChar; MaxLen: Integer); dcpcall;
function ContentGetSupportedField(FieldIndex: Integer; FieldName: PAnsiChar;
                                  Units: PAnsiChar; MaxLen: Integer): Integer; dcpcall;
function ContentGetValue(FileName: PAnsiChar; FieldIndex, UnitIndex: Integer;
                         FieldValue: PByte; MaxLen, Flags: Integer): Integer; dcpcall;

implementation

uses
  xpi_def, xpi_io;

const
  IDX_ID             = 0;
  IDX_NAME           = 1;
  IDX_VERSION        = 2;
  IDX_TYPE           = 3;
  IDX_DESCRIPTION    = 4;
  IDX_CREATOR        = 5;
  IDX_DEVELOPER      = 6;
  IDX_CONTRIBUTOR    = 7;
  IDX_TRANSLATOR     = 8;
  IDX_UNPACK         = 9;
  IDX_HOMEPAGEURL    = 10;
  IDX_UPDATEURL      = 11;
  IDX_PLATFORM       = 12;
  IDX_COMPATIBILITY  = 13;
  IDX_FIREFOX        = 14;
  IDX_MOZILLA        = 15;
  IDX_SEAMONKEY      = 16;
  IDX_THUNDERBIRD    = 17;
  IDX_FLOCK          = 18;
  IDX_SUNBIRD        = 19;
  IDX_FENNEC         = 20;
  IDX_PALEMOON       = 21;

  FIELD_COUNT        = 22;

var
  CurrentFileName: AnsiString;       // Current *.xpi file
  InstallManifest: TInstallManifest; // Cache for current file

function EnsureLength(S: AnsiString; MaxLen: Integer): AnsiString;
begin
  Result := S;
  if Length(Result) >= MaxLen then
    begin
      Result := Copy(Result, 1, MaxLen - 4);
      Result := Result + '...';
    end;
end;

procedure ContentGetDetectString(DetectString: PAnsiChar; MaxLen: Integer); dcpcall;
begin
  StrPLCopy(DetectString, 'EXT="XPI"', MaxLen)
end;

function ContentGetSupportedField(FieldIndex: Integer; FieldName: PAnsiChar;
  Units: PAnsiChar; MaxLen: Integer): Integer; dcpcall;
var
  Field: AnsiString;
begin
  StrPCopy(Units, EmptyStr);

  if FieldIndex >= FIELD_COUNT then
  begin
    Result := FT_NOMOREFIELDS;
    Exit;
  end;

  if (FieldIndex = IDX_UNPACK) then
  begin
    StrPLCopy(FieldName, 'Unpack', MaxLen);
    Exit(FT_BOOLEAN);
  end;

  Result := FT_STRING;

  case FieldIndex of
      IDX_ID:                   Field := 'ID';
      IDX_NAME:                 Field := 'Name';
      IDX_VERSION:              Field := 'Version';
      IDX_TYPE:                 Field := 'Type';
      IDX_DESCRIPTION:          Field := 'Description';
      IDX_CREATOR:              Field := 'Creator';
      IDX_DEVELOPER:            Field := 'Developer(s)';
      IDX_CONTRIBUTOR:          Field := 'Contributor(s)';
      IDX_TRANSLATOR:           Field := 'Translator(s)';
      IDX_HOMEPAGEURL:          Field := 'Home page';
      IDX_UPDATEURL:            Field := 'Update URL';
      IDX_PLATFORM:             Field := 'Platform(s)';
      IDX_COMPATIBILITY:        Field := 'Compatibility';
      IDX_FIREFOX:              Field := 'Firefox';
      IDX_MOZILLA:              Field := 'Mozilla Suite';
      IDX_SEAMONKEY:            Field := 'SeaMonkey';
      IDX_THUNDERBIRD:          Field := 'Mozilla Thunderbird';
      IDX_FLOCK:                Field := 'Flock';
      IDX_SUNBIRD:              Field := 'Sunbird';
      IDX_FENNEC:               Field := 'Fennec';
      IDX_PALEMOON:             Field := 'Pale Moon';
  end;

  StrPLCopy(FieldName, Field, MaxLen);
end;

function ContentGetValue(FileName: PAnsiChar; FieldIndex, UnitIndex: Integer;
  FieldValue: PByte; MaxLen, Flags: Integer): Integer; dcpcall;
var
  Value : AnsiString;
begin
  Result := FT_FILEERROR;
  if not FileExists(FileName) then Exit;

  if CurrentFileName <> FileName then
  begin
    if not ParseInsatallManifest(FileName, InstallManifest) then Exit;
    CurrentFileName := FileName;
  end;

  if (FieldIndex >= FIELD_COUNT) then
  begin
    Result := FT_NOSUCHFIELD;
    Exit;
  end;

  if (FieldIndex = IDX_UNPACK) then
  begin
    PBoolean(FieldValue)^:= InstallManifest.Unpack;
    Exit(FT_BOOLEAN);
  end;

  Result := FT_STRING;

  case FieldIndex of
      IDX_ID:                Value := InstallManifest.ID;
      IDX_NAME:              Value := InstallManifest.Name;
      IDX_VERSION:           Value := InstallManifest.Version;
      IDX_TYPE:              Value := InstallManifest.AddonType;
      IDX_DESCRIPTION:       Value := InstallManifest.Description;
      IDX_CREATOR:           Value := InstallManifest.Creator;
      IDX_DEVELOPER:         Value := InstallManifest.Developer;
      IDX_CONTRIBUTOR:       Value := InstallManifest.Contributor;
      IDX_TRANSLATOR:        Value := InstallManifest.Translator;
      IDX_HOMEPAGEURL:       Value := InstallManifest.HomePageURL;
      IDX_UPDATEURL:         Value := InstallManifest.UpdateURL;
      IDX_PLATFORM:          Value := InstallManifest.TargetPlatform;
      IDX_COMPATIBILITY:     Value := InstallManifest.Compatibility;
      IDX_FIREFOX:           Value := InstallManifest.TargetApplication.Values[ApplicationList.Names[0]];
      IDX_MOZILLA:           Value := InstallManifest.TargetApplication.Values[ApplicationList.Names[1]];
      IDX_SEAMONKEY:         Value := InstallManifest.TargetApplication.Values[ApplicationList.Names[2]];
      IDX_THUNDERBIRD:       Value := InstallManifest.TargetApplication.Values[ApplicationList.Names[3]];
      IDX_FLOCK:             Value := InstallManifest.TargetApplication.Values[ApplicationList.Names[4]];
      IDX_SUNBIRD:           Value := InstallManifest.TargetApplication.Values[ApplicationList.Names[5]];
      IDX_FENNEC:            Value := InstallManifest.TargetApplication.Values[ApplicationList.Names[6]];
      IDX_PALEMOON:          Value := InstallManifest.TargetApplication.Values[ApplicationList.Names[7]];
  else
      Result := FT_FIELDEMPTY;
      Exit;
  end;

  StrPLCopy(PAnsiChar(FieldValue), EnsureLength(Value, MaxLen), MaxLen);
end;

initialization
  CurrentFileName := EmptyStr;
  InstallManifest := TInstallManifest.Create;

finalization
  FreeAndNil(InstallManifest);

end.

