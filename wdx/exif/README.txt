Exif plugin for Double Commander (Linux version). It is based on

----------------------------------------------------------------

Exif plugin version 2.7 for Total Commander

This plugin extracts digital camera data from JPG files, like exposure time.
Can be used in thumbnail view and custom column view, as well as in the
search and multi-rename functions.

Changelog:
20220120 Release 2.7
20220120 Added: Support for more RAW image types: .ORF, .RAW, .RW2, .ARW, .PEF, .RAF (to use, remove the plugin via Configuration - Options - Plugins - Content Plugins, then install it again)
20220119 Added: New GPS date field
20220119 Added: Convert aperture values from APEX format to F-Number: sqare root(2) to the power of the stored APEX value
20190517 Release 2.6
20190517 Added: Support for new Canon Raw 3 format CR3. To use with CR3, you need to uninstall the old plugin via Configuration - Options - Plugins - Content plugins!
20190517 Release 2.5
20190517 Added: New fields Compression and CompressionName for exif field Nr. 0x0103
20140916 Release 2.4
20140916 Added: GPS longitude/latitude of type "floating": increased digits after decimal point to 6
20140916 Fixed: The GPS longitude and latitude fields of type "Floating" would return invalid values if the "minute" part also contained fractional seconds
20140908 Fixed: Check whether read returns 0 bytes, otherwise we may get into an endless loop 
20110907 Release 2.3
20110907 Added: 64-bit support
20110907 Added: One more digit for arc seconds (latitude, longitude)
20110829 Release 2.2
20110829 Added: Support GPS data: Image direction (in degrees)
20101109 Release 2.1
20101109 Added: Support GPS data: Latitude, Longitude, Altitute (meter or feet), Timestamp
20101014 Release 2.0
20101014 Added: Support for extensions .DNG and .NEF: Plugin needs to be uninstalled and reinstalled to use them
20101007 Release 1.9
20101007 Fixed: Crash on some images with huge EXIF blocks containing e.g. large preview images
20100629 Release 1.8 final (unchanged)
20091016 Release 1.8 (beta)
20091016 Added: Unicode support (file names only)
20090318 Release 1.7 (beta)
20090318 Added: Merged additional fields from plugin branch 1.47 (author: Micha Petraru) into main plugin branch
20071122 Release 1.6
20071122 Fixed: Loading of .CR2 files didn't work
20071116 Added: New extension .CR2 for newer Canon RAW files (need to uninstall old plugin first)
20071116 Fixed: Find EXIF data even if other data comes before it, like Photoshop data or IPTC data
20050213 Fixed: Access violation caused by Nikon images (Buffer "data" was too small)
20041124 Fixed: MaxApertureValue uses APEX field type: RootOf(2)^value
20041124 Added: ApertureValue and ExposureTimeFraction(APEX) fields
20041116 Fixed: Division by zero error if nominator or denominator contained 0 (rational value)
20041027 Added: Better display of ExposureTimeFraction for jpegs which store it as 16666/1000000
20041020 Added: Support for files in JFIF mode
20041017 Added: Support for ISO field
20041017 Added: Support for Canon RAW image files
20041017 Fixed: jpg file wasn't closed after extracting the exif data!
