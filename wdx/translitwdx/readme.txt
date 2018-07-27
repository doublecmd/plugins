translitwdx.lua

Plugin may be used to transliterate Russian words with english characters (and back). It can be very useful for multirename purposes.

Also it may be used to fixes of names:
- replace reserved characters of Windows (<, >, :, ", \, |, ?, *) to "_", it can be useful if you are using Windows and Linux at the same time;
- replace diacritic letters to ASCII equivalents, i.e. "Â" to "A";
- decode an URL-encoded string, i.e. "%D0%9A%D0%BD%D0%B8%D0%B3%D0%B0" to "Книга";
- fixes encoding of names:
    Win1251 to UTF-8,
    UTF-8 to Win1251,
    KOI8R to Win1251,
    OEM866 to Win1251.

Now script works with filename without extension (without letters after last dot), except "Windows naming conventions". Folder names are passed as is.
