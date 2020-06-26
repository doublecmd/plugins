library diskdir;

{$mode delphi}
{$include calling.inc}

uses
  SysUtils, Classes, WcxPlugin, Extension, UnixUtil, BaseUnix;

var
  gStartupInfo: TExtensionStartupInfo;

const maxarchives=10;
type ArchiveRec=record
       ArchiveName:array[0..259] of char;
       SrcDir,LastCurDir:array[0..259] of char;
       CurrentFileName:array[0..259] of char;
       CurrentUnpSize:comp;
       CurrentFileTime:longint;
       ArchiveHandle:textfile;
       ArchiveMode:longint;
       ArchiveActive:boolean;
       ChangeVolProc:TChangeVolProc;
       ProcessDataProc:TProcessDataProc;
     end;

{Keep a list of currently open archives (up to a maximum of maxarchives)}
var ArchiveList:array[1..maxarchives] of ArchiveRec;

FUNCTION StrTokR(Source: pchar; Token: ANSICHAR; var _TokenSource: pchar): pchar;
  VAR P: pchar;
BEGIN
  IF Source <> Nil THEN _TokenSource := Source;
  IF _TokenSource = Nil THEN begin
    StrTokR:=nil;
    exit
  end;
  P := StrScan(_TokenSource, Token);
  StrTokR := _TokenSource;
  IF P <> Nil THEN BEGIN
    P^ := #0;
    Inc(P);
  END;
  _TokenSource := P;
END;

function OpenArchive(var ArchiveData:tOpenArchiveData):TArcHandle; dcpcall;
var i:integer;
    archandle:TArcHandle;
    foundslot:boolean;
begin
  result:=0;
  foundslot:=false;
  for i:=1 to maxarchives do begin
    if not ArchiveList[i].ArchiveActive then begin
      foundslot:=true;
      archandle:=i;
      break;
    end;
  end;
  if not foundslot then begin
    ArchiveData.OpenResult:=E_NO_MEMORY;
    exit
  end;
  with ArchiveList[archandle] do begin
    LastCurDir[0]:=#0;
    strlcopy(ArchiveName,ArchiveData.arcName,sizeof(ArchiveName)-1);
    assignfile(ArchiveHandle, AnsiString(ArchiveName));
    filemode:=0;
    {$i-}
    reset(ArchiveHandle);
    {$i+}
    if ioresult<>0 then
      ArchiveData.OpenResult:=E_EOPEN
    else begin
      ArchiveActive:=true;
      SrcDir[0]:=#0;
      result:=archandle;
    end;
  end;
end;

function CloseArchive(hArcData:TArcHandle):longint; dcpcall;
begin
  with ArchiveList[hArcData] do begin
    if ArchiveActive then begin
      {$i-}
      Close(ArchiveHandle);
      {$i+}
      if ioresult<>0 then
        Result:=E_ECLOSE
      else
        Result:=0;
      ArchiveActive:=false;
    end;
  end;
end;

type
  TDateTime1 = packed record
    Year, Month, Day, Hour, Min, Sec: Word;
  end;

procedure PackTime(var T: TDateTime1; var P: Longint);
begin
  P:= LocalToEpoch(T.Year, T.Month, T.Day, T.Hour, T.Min, T.Sec);
end;

procedure UnpackTime(P: Longint; var T: TDateTime1);
begin
  EpochToLocal(P, T.Year, T.Month, T.Day, T.Hour, T.Min, T.Sec);
end;

function ReadHeader(hArcData:TArcHandle;var HeaderData:THeaderData):longint; dcpcall;
begin
  Result:=E_NOT_SUPPORTED;
end;

function makecomp(lowlong,highlong:longint):comp;
type tcomprec=record
       case boolean of
         true:(l,h:longint);
         false:(cmp:comp);
       end;
var tcr:tcomprec;
begin
  tcr.l:=lowlong;
  tcr.h:=highlong;
  result:=tcr.cmp;
end;

type tcomprec=record
       case boolean of
         true:(l,h:longint);
         false:(cmp:comp);
       end;

function ReadHeaderEx(hArcData:TArcHandle;var HeaderData:THeaderDataEx):longint; dcpcall;
var buf:array[0..1023] of char;
    buf1:array[0..259] of char;
    p,p1,psize,pdate,ptime:pchar;
    tdt:TDateTime1;
    code:integer;
    UnpSizeComp:comp;
    saveptr: pchar = nil;
begin
  if ArchiveList[hArcData].ArchiveActive then begin
    if not ArchiveList[hArcData].ArchiveActive then
      Result:=E_BAD_ARCHIVE
    else if eof(ArchiveList[hArcData].ArchiveHandle) then
      Result:=E_END_ARCHIVE
    else begin
      {$i-}
      readln(ArchiveList[hArcData].ArchiveHandle,buf);
      if ArchiveList[hArcData].SrcDir[0]=#0 then begin   {First line!}
        if strscan(buf,#9)=nil then begin
          strcopy(ArchiveList[hArcData].SrcDir,buf);
          readln(ArchiveList[hArcData].ArchiveHandle,buf);
        end;
      end;
      {$i+}
      if ioresult=0 then begin
        fillchar(HeaderData,sizeof(HeaderData)-16,#0);
        p:=StrTokR(buf,#9, saveptr);
        psize:=StrTokR(nil,#9, saveptr);
        pdate:=StrTokR(nil,#9, saveptr);
        ptime:=StrTokR(nil,#9, saveptr);
        if (buf[0]<>#0) and (buf[strlen(buf)-1]=PathDelim) then
          HeaderData.FileAttr:= S_IFDIR;
        if strscan(buf,PathDelim)=nil then begin  {No directory -> assume last given dir!}
          strlcopy(buf1,ArchiveList[hArcData].LastCurDir,sizeof(HeaderData.FileName)-1);
        end else begin
          if HeaderData.FileAttr = S_IFDIR then
            strlcopy(ArchiveList[hArcData].LastCurDir,buf,sizeof(ArchiveList[hArcData].lastcurdir)-1);
          buf1[0]:=#0;
        end;
        strlcat(buf1,buf,sizeof(buf1)-1);
        strlcopy(HeaderData.FileName,buf1,sizeof(HeaderData.FileName));
        if ArchiveList[hArcData].SrcDir[0]=#0 then   {First line!}
          strlcopy(ArchiveList[hArcData].SrcDir,buf1,3);

        strlcopy(HeaderData.ArcName,ArchiveList[hArcData].ArchiveName,sizeof(HeaderData.ArcName)-1);

        if psize<>nil then begin
          val(psize,UnpSizeComp,code);
          if code<>0 then begin
            HeaderData.UnpSize:= Cardinal(-1);
            HeaderData.UnpSizeHigh:= Cardinal(-1);
          end else begin
            HeaderData.UnpSize:=tcomprec(UnpSizeComp).l;
            HeaderData.UnpSizeHigh:=tcomprec(UnpSizeComp).h;
          end;
          HeaderData.PackSize:=HeaderData.UnpSize;
        end else begin
          HeaderData.UnpSize:= Cardinal(-1);
          HeaderData.UnpSizeHigh:= Cardinal(-1);
        end;
        HeaderData.PackSizeHigh:=HeaderData.UnpSizeHigh;
        HeaderData.FileTime:=-1;
        if pdate<>nil then begin {Year.month.day}
          fillchar(tdt,sizeof(tdt),#0);
          p1:=StrTokR(pdate,'.', saveptr);
          val(p1,tdt.year,code);
          p1:=StrTokR(nil,'.', saveptr);
          if (code=0) and (p1<>nil) then begin
            if tdt.year<1900 then if tdt.year<80 then inc(tdt.year,2000)
                                                 else inc(tdt.year,1900);
            val(p1,tdt.month,code);
            p1:=StrTokR(nil,'.', saveptr);
            if (code=0) and (p1<>nil) then begin
              val(p1,tdt.day,code);
              if code=0 then begin
                if ptime<>nil then begin {hour:min:seconds}
                  p1:=StrTokR(ptime,':', saveptr);
                  val(p1,tdt.hour,code);
                  p1:=StrTokR(nil,'.', saveptr);
                  if (code=0) and (p1<>nil) then begin
                    val(p1,tdt.min,code);
                    p1:=StrTokR(nil,'.', saveptr);
                    if (code=0) then
                      if (p1<>nil) then val(p1,tdt.sec,code);
                  end;
                end;
                packtime(tdt,HeaderData.FileTime);
              end;
            end;
          end;
        end;
        strlcopy(ArchiveList[hArcData].CurrentFileName,HeaderData.filename,sizeof(ArchiveList[hArcData].CurrentFileName)-1);
        ArchiveList[hArcData].CurrentFileTime:=HeaderData.FileTime;
        ArchiveList[hArcData].CurrentUnpSize:=makecomp(HeaderData.UnpSize,
          HeaderData.UnpSizeHigh);
        Result:=0;
      end else
        Result:=E_EREAD;
    end;
  end;
end;

function createvalidoutfile(var g:file;trgfile:pchar;isadir:boolean):integer;
var err:integer;
    buf:array[0..259] of char;
    p,p1:pchar;
begin
  {$i-}
  if not isadir then begin
    assign(g,trgfile);
    rewrite(g,1);
    err:=ioresult;
  end;
  strlcopy(buf,trgfile,sizeof(buf)-1);
  if (err=3) or isadir then begin  {path not found}
    p1:=strrscan(buf,PathDelim);
    if p1<>nil then inc(p1);  {pointer to filename}
    p:=strscan(buf,PathDelim);
    if p<>nil then p:=strscan(p+1,PathDelim);
    while (p<>nil) and (p<p1) do begin
      p[0]:=#0;
      mkdir(strpas(buf));
      err:=ioresult;
      p[0]:=PathDelim;
      p:=strscan(p+1,PathDelim);
    end;
    if isadir then begin
      result:=0;    {A directory -> ok}
      exit;
    end;
    rewrite(g,1);
    err:=ioresult;
  end;
  result:=err;
end;

function ProcessFile(hArcData:TArcHandle;Operation:longint;DestPath,DestName:pchar):longint; dcpcall;
var srcfile,trgfile:array[0..259] of char;
    f,g:file;
    buf:array[0..32767] of char;
    p:pchar;
    dataread,datawritten,err:integer;
    showprogress:boolean;
begin                   
  if (Operation=PK_EXTRACT) or (Operation=PK_TEST) then
  with ArchiveList[hArcData] do if ArchiveActive then begin
    strcopy(srcfile,srcdir);
    strlcat(srcfile,CurrentFilename,sizeof(srcfile)-1);
    filemode:=0;
    repeat
      assign(f, AnsiString(srcfile));
      {$i-}
      reset(f,1);
      {$i+}
      err:=ioresult;
      if err>0 then
        if (@ChangeVolProc=nil) or
          (ChangeVolProc(srcfile,0)=0) then err:=-1
        else begin           {Remeber path given by user!}
          p:=strpos(srcfile,CurrentFilename);
          if p<>nil then
            strlcopy(srcdir,srcfile,p-PChar(@srcfile));
        end;
    until err<=0;

    if err<>0 then begin result:=E_EOPEN; exit; end;

    showprogress:=(CurrentUnpSize>0) and (@ProcessDataProc<>nil);
    if destpath<>nil then begin
      strcopy(trgfile,destpath);
      if (trgfile[0]<>#0) and (trgfile[strlen(trgfile)-1]<>PathDelim) then
        strcat(trgfile,PathDelim);
    end else trgfile[0]:=#0;
    if (Operation=PK_EXTRACT) then begin
      if destname<>nil then
        strlcat(trgfile,destname,sizeof(trgfile)-1)
      else begin
        p:=strrscan(CurrentFileName,PathDelim);
        if p<>nil then inc(p) else p:=CurrentFileName;
        strlcat(trgfile,CurrentFileName,sizeof(trgfile)-1);
      end;
      if createvalidoutfile(g,trgfile,false)<>0 then begin close(f); result:=E_ECREATE; exit; end;
    end;
    while true do begin
      {$i-}
      blockread(f,buf[0],sizeof(buf),dataread);
      if ioresult<>0 then begin
        if (Operation=PK_EXTRACT) then close(g);
        result:=E_EREAD;
        exit
      end else begin
        if (Operation=PK_EXTRACT) then begin
          blockwrite(g,buf[0],dataread,datawritten);
          if ioresult<>0 then begin
            close(g);
            result:=E_EWRITE;
            exit
          end
        end else
          datawritten:=dataread;
        if showprogress then begin
          if ProcessDataProc(@buf,datawritten)=0 then begin {Abort!}
            close(f);
            if (Operation=PK_EXTRACT) then close(g);
            result:=0;     {UnRAR.DLL returns no error when aborted!}
            exit;
          end;
        end;
      end;
      if dataread<sizeof(buf) then break;
      {$i+}
    end;
    close(f);
    if (Operation=PK_EXTRACT) then begin
      FileSetDate(tfilerec(g).handle,CurrentFileTime);
      close(g);
    end;
  end;
  Result:=0;
end;

var PackerChangeVolProc:TChangeVolProc;
    PackerProcessDataProc:TProcessDataProc;

function SetChangeVolProc(hArcData:TArcHandle;ChangeVolProc1:TChangeVolProc):longint; dcpcall;
begin
  Result:= 0;
  if hArcData = wcxInvalidHandle then
    PackerChangeVolProc:=ChangeVolProc1
  else
    ArchiveList[hArcData].ChangeVolProc:=ChangeVolProc1;
end;

function SetProcessDataProc(hArcData:TArcHandle;ProcessDataProc1:TProcessDataProc):longint; dcpcall;
begin
  Result:= 0;
  if hArcData = wcxInvalidHandle then
    PackerProcessDataProc:=ProcessDataProc1
  else
    ArchiveList[hArcData].ProcessDataProc:=ProcessDataProc1;
end;

function PackFiles(PackedFile,SubPath,SrcPath,AddList:pchar;Flags:integer):integer; dcpcall;
var s:tsearchrec;
    PackedHandle:text;
    p,pEnd,pData:pchar;
    tdt:tdatetime1;
    buf:array[0..299] of char;
    RunOk:boolean;
begin
  if FileExists(PackedFile) then begin
    result:=E_NOT_SUPPORTED;
    exit
  end else begin
    assignfile(PackedHandle,PackedFile);
    filemode:=0;
    {$i-}
    rewrite(PackedHandle);
    {$i+}
    if ioresult<>0 then
      Result:=E_ECREATE
    else begin
      if SrcPath[0]<>#0 then            {Search result?}
        writeln(PackedHandle,SrcPath);
      p:=AddList;
      strcopy(buf,SrcPath);
      pEnd:=Strend(buf);
      RunOK:=true;
      while (p[0]<>#0) and RunOK do begin
        strlcat(buf,p,sizeof(buf)-1);
        if buf[strlen(buf)-1]=PathDelim then
          buf[strlen(buf)-1]:=#0;
        if FindFirst(strpas(buf),faAnyFile,s)=0 then begin
          pData:=pEnd;
          if (s.attr and fadirectory<>0) then begin
            strlcat(buf,PathDelim,sizeof(buf)-1);
            s.size:=0;
          end else if SrcPath[0]<>#0 then begin               {Store only file names!}
            pData:=strrscan(pEnd,PathDelim);
            if pData<>nil then inc(pData)
                          else pData:=pEnd;
          end;
          Unpacktime(s.time,tdt);
          writeln(PackedHandle,strpas(pData)+#9+FloatToStrF(s.size, ffFixed,18,0)+#9+
          inttostr(tdt.year)+'.'+inttostr(tdt.month)+'.'+inttostr(tdt.day)+#9+
          inttostr(tdt.hour)+':'+inttostr(tdt.min)+'.'+inttostr(tdt.sec));
          sysutils.FindClose(s);
          if @PackerProcessDataProc<>nil then
            if PackerProcessDataProc(PChar(pEnd),s.size)=0 then
              RunOK:=false;
        end;
        pEnd[0]:=#0;
        p:=strend(p)+1;
      end;
      CloseFile(PackedHandle);
    end;
  end;
  if RunOK then result:=0
           else result:=E_EABORTED;
end;

function DeleteFiles(PackedFile,DeleteList:pchar):integer; dcpcall;
begin
  result:=E_NOT_SUPPORTED;
end;

function GetPackerCaps:integer; dcpcall;
begin
  result:=PK_CAPS_NEW or PK_CAPS_MULTIPLE or PK_CAPS_OPTIONS;
end;

procedure ConfigurePacker(ParentHandle: HWND; DllInstance:thandle); dcpcall;
begin
  gStartupInfo.MessageBox('Diskdir plugin, Copyright © 1999-2012 by Christian Ghisler' + LineEnding +
  LineEnding + 'Linux version, Copyright © 2016 by Alexander Koblov',
    'About diskdir',mb_iconinformation);
end;

procedure ExtensionInitialize(StartupInfo: PExtensionStartupInfo); dcpcall;
begin
  gStartupInfo:= StartupInfo^;
end;

exports
  OpenArchive,
  CloseArchive,
  ReadHeader,
  ReadHeaderEx,
  ProcessFile,
  SetChangeVolProc,
  SetProcessDataProc,
  PackFiles,
  DeleteFiles,
  GetPackerCaps,
  ConfigurePacker,
  ExtensionInitialize;

begin
  FillChar(ArchiveList,sizeof(ArchiveList),#0);
end.

