unit SProc;

{$IFDEF FPC}
  {$MODE Delphi}
{$ENDIF}

interface

procedure SReplace(var s: string; const sfrom, sto: string);
procedure SReplaceI(var s: string; const sfrom, sto: string);
procedure SReplaceAll(var s: string; const sfrom, sto: string);
procedure SReplaceIAll(var s: string; const sfrom, sto: string);

function SDeleteFrom(const s, sfrom: string): string;
function SDeleteTo(const s, sto: string): string;
function SBetween(const s, s1, s2: string): string;
function STail(const s: string; len: integer): string;
function SDeleteTags(const s: string): string;
function PosFrom(const SubStr, s: string; FromPos: integer): integer;
function Pos2(const SubStr1, SubStr2, s: string): integer;
function Pos2From(const SubStr1, SubStr2, s: string; FromPos: integer): integer;
function SCopyFromTo(const s: string; Pos: integer; const sTo: string): string;
function Min(n1, n2: integer): integer;

type
  TDecodeRec = record SFrom, STo: string; end;
  TDecodeProgress = procedure(N: integer);

function SDecode(const s: string; const Decode: array of TDecodeRec; Progress: TDecodeProgress): string;
function SDecodeBase64(const CinLine: string): string;


implementation

uses
  SysUtils;

procedure SReplace(var s: string; const sfrom, sto: string);
var
  i: integer;
begin
  i:= Pos(sfrom, s);
  if i>0 then
    begin
    Delete(s, i, Length(sfrom));
    Insert(sto, s, i);
    end;
end;

procedure SReplaceI(var s: string; const sfrom, sto: string);
var
  i: integer;
begin
  i:= Pos(LowerCase(sfrom), LowerCase(s));
  if i>0 then
    begin
    Delete(s, i, Length(sfrom));
    Insert(sto, s, i);
    end;
end;

procedure SReplaceAll(var s: string; const sfrom, sto: string);
var
  i: integer;
begin
  repeat
    i:= Pos(sfrom, s);
    if i=0 then Break;
    Delete(s, i, Length(sfrom));
    Insert(sto, s, i);
  until false;
end;

procedure SReplaceIAll(var s: string; const sfrom, sto: string);
var
  i: integer;
begin
  repeat
    i:= Pos(LowerCase(sfrom), LowerCase(s));
    if i=0 then Break;
    Delete(s, i, Length(sfrom));
    Insert(sto, s, i);
  until false;
end;


function SDeleteFrom(const s, sfrom: string): string;
var
  i: integer;
begin
  i:= Pos(sfrom, s);
  if i=0
    then Result:= s
    else Result:= Copy(s, 1, i-1);
end;

function SDeleteTo(const s, sto: string): string;
var
  i: integer;
begin
  Result:= s;
  i:= Pos(sto, s);
  if i>0 then Delete(Result, 1, i+Length(sto)-1);
end;

function SBetween(const s, s1, s2: string): string;
var
  n1, n2: integer;
begin
  Result:= '';
  n1:= Pos(s1, s);                     if n1=0 then Exit;
  n2:= Pos(s2, Copy(s, n1+1, MaxInt)); if n2=0 then Exit;
  Result:= Copy(s, n1+Length(s1), n2-Length(s1));
end;

function STail(const s: string; len: integer): string;
begin
  if Length(s)<=len
    then Result:= s
    else Result:= Copy(s, Length(s)-len+1, len);
end;

function SDeleteTags(const s: string): string;
var
  n1, n2: integer;
begin
  Result:= s;
  repeat
    n1:= Pos('<', Result);
    if n1=0 then Break;
    n2:= PosFrom('>', Result, n1+1);
    if n2=0 then Break;
    Delete(Result, n1, n2-n1+1);
  until false;
end;



function PosFrom(const SubStr, s: string; FromPos: integer): integer;
var
  n: integer;
begin
  for n:= FromPos to Length(s)-Length(SubStr)+1 do
    if SubStr=Copy(s, n, Length(SubStr)) then
      begin Result:= n; Exit end;
  Result:= 0;
end;

function Pos2(const SubStr1, SubStr2, s: string): integer;
begin
  Result:= Pos2From(SubStr1, SubStr2, s, 1);
end;

function Pos2From(const SubStr1, SubStr2, s: string; FromPos: integer): integer;
var
  n: integer;
begin
  for n:= FromPos to Length(s) do
    if (SubStr1=Copy(s, n, Length(SubStr1))) or
       (SubStr2=Copy(s, n, Length(SubStr2))) then
      begin Result:= n; Exit end;
  Result:= 0;
end;

function Min(n1, n2: integer): integer;
begin
  if n1<n2 then Result:= n1 else Result:= n2;
end;

function SDecode(const s: string; const Decode: array of TDecodeRec; Progress: TDecodeProgress): string;
var
  i, j: integer;
  DoDecode: boolean;
begin
  Result:= '';
  i:= 1;
  repeat
    if i>Length(s) then Break;
    DoDecode:= false;
    for j:= Low(decode) to High(decode) do
      with Decode[j] do
        if SFrom=Copy(s, i, Length(SFrom)) then
          begin
          DoDecode:= true;
          Result:= Result+STo;
          Inc(i, Length(SFrom));
          Break
          end;
    if DoDecode then Continue;
    Result:= Result+s[i];
    Inc(i);

    if Assigned(Progress) then
      Progress(i*100 div Length(s));
  until false;
end;

function SCopyFromTo(const s: string; Pos: integer; const sTo: string): string;
var
  n: integer;
begin
  n:= PosFrom(sTo, s, Pos);
  Result:= Copy(s, Pos, n-Pos);
end;

// http://forum.wincmd.ru/viewtopic.php?p=36888#36888
function SDecodeBase64(const CinLine: string): string;
const
  RESULT_ERROR = -2;
var
  inLineIndex: Integer;
  c: Char;
  x: SmallInt;
  c4: Word;
  StoredC4: array[0..3] of SmallInt;
  InLineLength: Integer;
begin
  Result := '';
  inLineIndex := 1;
  c4 := 0;
  InLineLength := Length(CinLine);

  while inLineIndex <= InLineLength do
  begin
    while (inLineIndex <= InLineLength) and (c4 < 4) do
    begin
      c := CinLine[inLineIndex];
      case c of
        '+'     : x := 62;
        '/'     : x := 63;
        '0'..'9': x := Ord(c) - (Ord('0')-52);
        '='     : x := -1;
        'A'..'Z': x := Ord(c) - Ord('A');
        'a'..'z': x := Ord(c) - (Ord('a')-26);
      else
        x := RESULT_ERROR;
      end;
      if x <> RESULT_ERROR then
      begin
        StoredC4[c4] := x;
        Inc(c4);
      end;
      Inc(inLineIndex);
    end;

    if c4 = 4 then
    begin
      c4 := 0;
      Result := Result + Char((StoredC4[0] shl 2) or (StoredC4[1] shr 4));
      if StoredC4[2] = -1 then Exit;
      Result := Result + Char((StoredC4[1] shl 4) or (StoredC4[2] shr 2));
      if StoredC4[3] = -1 then Exit;
      Result := Result + Char((StoredC4[2] shl 6) or (StoredC4[3]));
    end;
  end;
end;

end.
