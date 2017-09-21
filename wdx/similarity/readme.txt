Similarity content plugin for Double Commander. It is based on

------------------------------------------------------------------

Similarity 0.02 - WDX plugin
© 2008 fenixproductions
© 2013 Konstantin Vlasov
--------------------------

1. General information
2. Credits
3. Thanks
4. Changelog
5. What's new
6. Installation
7. Licence
8. TODO

--------------------------
1. General information
--------------------------

Similarity is Total Commander's content plugin for showing strings similarity.
The basic calculation are made with file names and phrase specified in INI file.

The purpose of this plugin is to make TC search more flexible via algorithms used in spell checking tools.

You can use this plugin for custom columns view but the best way is to have it in mind during "Find files" procedure.

For fields description go to #4.

--------------------------
2. Credits
--------------------------

This dirty code has been written by fenixproductions.

Some parts of code were taken from:

http://www.codeproject.com/

simil.h:

http://www.codeproject.com/KB/string/SimilarStrings.aspx

DEELX - Regular Expression Engine:

http://www.codeproject.com/KB/library/deelx.aspx?display=Print

General info about "string metrics":

http://www.dcs.shef.ac.uk/~sam/stringmetrics.html

If more used links bring to my mind I will give them here.

--------------------------
3. Thanks
--------------------------

- kamil_en from who I get to know about Levenshtein distance
- Sam's String Metrics site

--------------------------
4. Changelog
--------------------------

0.02 (by Konstantin Vlasov):
- Rebuilt for 64-bit architecture.
- Placing leven.ini into the first of the following locations where writing is permitted:
  * plugin's directory;
  * Total Commander installation directory;
  * wincmd.ini location.
- Minor optimizations.

0.01 (by fenixproductions):
Added supported fields:
- distanceLev - Levenshtein distance between filename and phrase,
- similarityLev - similarity computed using values from above,
- simil1 - using Rui A. Rebelo's method (see links in Credits),
- simil2 - same as above but arguments are switched,
- similRO - using Ratcliff/Obershelp pattern recognition,
- average - average value for methods from above with "50" added if "contains" is true,
- contains - returns 1 if filename contains phrase (0 otherwise),

--------------------------
5. What's new
--------------------------

Nothing more than above.

--------------------------
6. Installation
--------------------------

Use TC plugin autoinstallation feature, or unpack the archive and install
the plugin manually using the dialog:
Configuration -> Plugins -> Content plugins (.WDX) -> Configure...

Then add em_XXX command or new button for similarity.exe.
Specify ? as parameter (just question mark).

--------------------------
7. Licence
--------------------------

You can do whatever you want with this.
It might be nice to give a note about me if you change a code, after all.

--------------------------
8. TODO
--------------------------

Long time no see for C/C++ so code clean-up is obvious.
