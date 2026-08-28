unit uHttpCheck;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, uPlugmanJson;

const
  UPDATE_DISCLAIMER =
    'The remote file has changed. It cannot be verified if this is a newer version or a working build.';

type
  THeadResult = record
    Success: Boolean;
    SupportsHead: Boolean;
    ContentLength: Int64;
    ETag: string;
    LastModified: string;
    ErrorMessage: string;
  end;

function ProbeUrl(const Url: string): THeadResult;
function DownloadUrl(const Url, DestFile: string; out Head: THeadResult): Boolean;
function HasRemoteChange(const Entry: TPluginRecord; const Head: THeadResult): Boolean;

implementation

uses
  fphttpclient, opensslsockets;

function HeaderValue(Headers: TStrings; const Name: string): string;
var
  I: Integer;
  P: Integer;
  Line, Key: string;
begin
  Result := '';
  if Headers = nil then Exit;
  for I := 0 to Headers.Count - 1 do
  begin
    Line := Headers[I];
    P := Pos(':', Line);
    if P > 0 then
    begin
      Key := Trim(Copy(Line, 1, P - 1));
      if SameText(Key, Name) then
        Exit(Trim(Copy(Line, P + 1, MaxInt)));
    end;
  end;
end;

function ProbeUrl(const Url: string): THeadResult;
var
  Client: TFPHTTPClient;
  RespHeaders: TStringList;
begin
  Result.Success := False;
  Result.SupportsHead := True;
  Result.ContentLength := -1;
  Result.ETag := '';
  Result.LastModified := '';
  Result.ErrorMessage := '';
  Client := TFPHTTPClient.Create(nil);
  RespHeaders := TStringList.Create;
  try
    try
      Client.AddHeader('User-Agent', 'DoubleCmd-Plugman/1.0');
      Client.Head(Url, RespHeaders);
      Result.Success := (Client.ResponseStatusCode >= 200) and
        (Client.ResponseStatusCode < 300);
      Result.ETag := HeaderValue(RespHeaders, 'ETag');
      Result.LastModified := HeaderValue(RespHeaders, 'Last-Modified');
      Result.ContentLength :=
        StrToInt64Def(HeaderValue(RespHeaders, 'Content-Length'), -1);
    except
      on E: Exception do
      begin
        Result.ErrorMessage := E.Message;
        Result.SupportsHead := False;
      end;
    end;
  finally
    RespHeaders.Free;
    Client.Free;
  end;
end;

function DownloadUrl(const Url, DestFile: string; out Head: THeadResult): Boolean;
var
  Client: TFPHTTPClient;
  Stream: TFileStream;
begin
  Result := False;
  Head := ProbeUrl(Url);
  Client := TFPHTTPClient.Create(nil);
  Stream := TFileStream.Create(DestFile, fmCreate);
  try
    try
      Client.AddHeader('User-Agent', 'DoubleCmd-Plugman/1.0');
      Client.Get(Url, Stream);
      Result := (Client.ResponseStatusCode >= 200) and
        (Client.ResponseStatusCode < 300);
      Head.ETag := HeaderValue(Client.ResponseHeaders, 'ETag');
      Head.LastModified := HeaderValue(Client.ResponseHeaders, 'Last-Modified');
      Head.ContentLength :=
        StrToInt64Def(HeaderValue(Client.ResponseHeaders, 'Content-Length'), Stream.Size);
      if Head.ContentLength < 0 then
        Head.ContentLength := Stream.Size;
      Head.Success := Result;
    except
      on E: Exception do
        Head.ErrorMessage := E.Message;
    end;
  finally
    Stream.Free;
    Client.Free;
  end;
end;

function HasRemoteChange(const Entry: TPluginRecord; const Head: THeadResult): Boolean;
begin
  Result := False;
  if not Head.Success then Exit(False);
  if (Entry.State.ETag <> '') and (Head.ETag <> '') then
    Exit(not SameText(Entry.State.ETag, Head.ETag));
  if (Entry.State.LastModified <> '') and (Head.LastModified <> '') then
    Exit(not SameText(Entry.State.LastModified, Head.LastModified));
  if (Entry.State.LocalSize > 0) and (Head.ContentLength > 0) then
    Exit(Entry.State.LocalSize <> Head.ContentLength);
  Result := True;
end;

end.
