translitwdx.lua (cross-platform)
2019.03.29

Similar to Translit plugin for Total Commander by Pavel Dubrovsky https://github.com/pozitronik/wdx_translit

Plugin may be used to transliterate Russian words with English characters (and back). It can be very useful for multirename purposes.

Also it may be used to fix filenames:
- replace reserved characters of Windows (<, >, :, ", \, |, ?, *) to "_", it can be useful if you are using Windows and Linux at the same time;
- replace diacritic letters to ASCII equivalents, i.e. "Â" to "A";
- decode an URL-encoded string, i.e. "%D0%9A%D0%BD%D0%B8%D0%B3%D0%B0" to "Книга";
- fixes encoding of names:
    Win1251 to UTF-8,
    UTF-8 to Win1251,
    KOI8R to Win1251,
    OEM866 to Win1251.

Script works with filename without extension (without letters after last dot), except "Windows naming conventions". Folder names are passed as is.

Транслитерация:
- Rus2Lat и Lat2Rus (ГОСТ 7_79-2000)
  Транслитерация ГОСТ 7.79-2000 http://transliteration.ru/gost-7-79-2000/
- Rus2Lat и Lat2Rus (Загранпаспорт РФ)
  Транслитерация имён для загранпаспорта РФ. http://transliteration.ru/mvd_1047/
- Rus2Lat и Lat2Rus (MP3 Навигатор 2)
  Основа взята из программы MP3 Навигатор 2 by Иван Никитин
  Корректировка таблицы: Павел Дубровский aka D1P
- Rus2Lat и Lat2Rus (Колхоз)
  Латинско-русский транслит для названий книг из библиотеки Колхоза. Версия 1.0
  Автор: Сивцов Иван aka Melirius
- Rus2Lat и Lat2Rus (calc.ru, без ЪЬ)
  Латинско-русский транслит https://www.calc.ru/ Без "Ъ" и "Ь"!
- Rus2Lat и Lat2Rus (SMS)
  Русско-латинская транслитная перекодировка для SMS-сообщений
  Автор: Le
- Rus2Lat и Lat2Rus (РИНТ)
  Транслитерация по системе РИНТ (Русский ИНТернет).
  Система РИНТ (Русский + ИНТернет) представляет собой письменность русского языка на графической основе латинского алфавита. Она предназначена для интернет-пользователей и способствует распространению текстов на русском языке при отсутствии возможности использования русского алфавита (кириллицы).
  Автор: Павел Дубровский aka D1P

Исправление имён файлов:
- Diacritic to ASCII
  Замена диакритики эквивалентами из ASCII, таблица из скрипта replaceDiacriticLetters.js для AkelPad (автор: Infocatcher)
- URL to Text
  URL-декодирование: например, имена сохранённых веб-страниц - "%D0%9A%D0%BD%D0%B8%D0%B3%D0%B0" в "Книга"
  http://lua-users.org/wiki/StringRecipes
- Win1251 to UTF-8
  Таблица перекодировки Win1251 в UTF-8 (автор: Vitaly Valitsky)
- UTF-8 to Win1251
  Таблица перекодировки UTF-8 в Win1251 (автор: Vitaly Valitsky)
- KOI8-R to Win1251
  Таблица перекодировки KOI8R в Win1251 (автор: Evil Angel)
- OEM866 to Win1251
  Таблица перекодировки DOS866 в WIN1251 (втор: Павел Дубровский aka D1P)
