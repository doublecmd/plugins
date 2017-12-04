#!/bin/bash

#(EXT="ISO")|(EXT="TORRENT")|(EXT="SO")|(EXT="MO")|(EXT="DEB")|(EXT="TAR")|(EXT="LHA")|(EXT="ARJ")|(EXT="CAB")|(EXT="HA")|(EXT="RAR")|(EXT="ALZ")|(EXT="CPIO")|(EXT="7Z")|(EXT="ACE")|(EXT="ARC")|(EXT="ZIP")|(EXT="ZOO")|(EXT="PS")|(EXT="PDF")|(EXT="ODT")|(EXT="DOC")|(EXT="XLS")|(EXT="DVI")|(EXT="DJVU")|(EXT="EPUB")|(EXT="HTML")|(EXT="HTM")

file=$1
filetype="${1##*.}"
echo "$filetype"

case "${filetype}" in
	[Tt][Oo][Rr][Rr][Ee][Nn][Tt])
		ctorrent -x "$file"
		;;
	[Ss][Oo])
		file "$file" && nm -C -D "$file"
		;;
	[Mm][Oo])
		msgunfmt "$file" || cat "$file"
		;;
	[Dd][Ee][Bb])
		dpkg-deb -I "$file" && echo && dpkg-deb -c "$file"
		;;



	[Tt][Aa][Rr])
		tar tvvf - < "${file}"
		;;
	[Ll][Hh][Aa])
		lha l "$file"
		;;
	[Aa][Rr][Jj])
		arj l "$file" 2>/dev/null || \
			unarj l "$file"
		;;
	[Cc][Aa][Bb])
		cabextract -l "$file"
		;;
	[Hh][Aa])
		ha lf "$file"
		;;
	[Rr][Aa][Rr])
		rar v -c- "$file" 2>/dev/null || \
			unrar v -c- "$file"
		;;
	[Aa][Ll][Zz])
		unalz -l "$file"
		;;
	[Cc][Pp][Ii][Oo])
		cpio -itv < "$file" 2>/dev/null
		;;
	7[Zz])
		7za l "$file" 2>/dev/null || 7z l "$file"
		;;
	[Aa][Cc][Ee])
		unace l "$file"
		;;
	[Aa][Rr][Cc])
		arc l "$file"
		;;
	[Zz][Ii][Pp])
		unzip -v "$file"
		;;
	[Zz][Oo][Oo])
		zoo l "$file"
		;;
		
		
		
	[Pp][Ss])
		ps2ascii "$file"
		;;
	[Pp][Dd][Ff])
		pdftotext -layout -nopgbrk "$file" -
		;;
	[Oo][Dd][Tt])
			odt2txt "$file"
		;;
	[Dd][Oo][Cc])
		which wvHtml >/dev/null 2>&1 &&
		{
			tmp=`mktemp -d ${TMPDIR:-/tmp}/%p.XXXXXX`
			wvHtml "$file" --targetdir="$tmp" page.html
			elinks -dump "$tmp/page.html"
			rm -rf "$tmp"
		} || \
			antiword -t "$file" || \
			catdoc -w "$file" || \
			word2x -f text "$file" - || \
			strings "$file"
		;;
	[Xx][Ll][Ss])
		which xlHtml >/dev/null 2>&1 && {
			tmp=`mktemp -d ${TMPDIR:-/tmp}/%p.XXXXXX`
			xlhtml -a "$file" > "$tmp/page.html"
			elinks -dump "$tmp/page.html"
			rm -rf "$tmp"
		} || \
			xls2csv "$file" || \
			strings "$file"
		;;
	[Dd][Vv][Ii])
		which dvi2tty >/dev/null 2>&1 && \
			dvi2tty "$file" || \
			catdvi "$file"
		;;
	[Dd][Jj][Vv][Uu])
		djvused -e print-pure-txt "$file"
		;;
	[Dd][Jj][Vv])
		djvused -e print-pure-txt "$file"
		;;
	[Ee][Pp][Uu][Bb])
		einfo -v "$file"
		;;



	[Ii][Ss][Oo])
		isoinfo -d -i "$file" && isoinfo -l -R -J -i "$file"
		;;
	[Hh][Tt][Mm][Ll]|[Hh][Tt][Mm])
		links -dump "$file" 2>/dev/null || w3m -dump "$file" 2>/dev/null || lynx -dump "$file"
		;;
		*)
				;;
		esac

