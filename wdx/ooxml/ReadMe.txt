Office Open XML content plugin for Double Commander. It is based on

------------------------------------------------------------------

Office2007.wdx plugin (0.0.3.1)
--------------------------------

= About =

Office2007 plugin allows to see Microsoft Ofice files properties in Total Commander.


= Supported files extensions =

+ Word 2007 XML
  - .docx - Document
  - .docm - Macro-Enabled Document
  - .dotx - Template
  - .dotm - Macro-Enabled Template	
+ Excel 2007 XML
  - .xlsx - Workbook
  - .xlsm - Macro-Enabled Workbook
  - .xltx - Template
  - .xltm - Macro-Enabled Template
  - .xlsb - Binary Workbook
  - .xlam - Macro-Enabled Add-In
+ PowerPoint 2007 XML
  - .pptx - Presentation
  - .pptm - Macro-Enabled Presentation
  - .potx - Template
  - .potm - Macro-Enabled Template
  - .ppam - Macro-Enabled Add-In
  - .ppsx - Show
  - .ppsm - Macro-Enabled Show
  
 Please note that by default only few extensions are set (docx, xlsx, pptx, ppsx).
 
 = Available fields =
 
 + Core properties
  - Category - document cathegory (ex. Resume, Letter)
  - ContentStatus - content's status (ex. Draft, Final)
  - ContentType - content's type (ex. Whitepaper)
  - Created - date and time of creation of the document
  - Creator - first document's author
  - Description - document's description
  - Identifier - reference to resource within given content
  - Keywords - document's keywords set
  - Language - document's content language
  - LastModifiedBy - author of last modifications
  - LastPrinted - date and time of last printing
  - Modified - date and time of last modifications
  - Revision - revision number (how many times document was changed)
  - Subject - the topic of the resource's content
  - Title - name given to the document
  - Version - document's version number
 
 + Application specific
  - AppVersion - application's version
  - Application - name of the application document was created with
  - Characters - total number of characters
  - CharactersWithSpaces - total number of characters with spaces
  - Company - name of Company
  - DigitalSig - is Digital Signature available
  - DigitalSigExt - is Extended Digital Signature available
  - DocSecurityLevel - Document Security Level
  - HiddenSlides - number of hidden slides
  - HyperlinkBase - relative hypelinks base
  - HyperlinksChanged - did hyperlinks change
  - Lines - number of lines
  - LinksUpToDate - are links up-to-date
  - MMClips - total number of Multimedia Clips
  - Manager - name of the Manager within Company
  - Notes - number of pages containing notes
  - Pages - total number of pages
  - Paragraphs - total number of paragraphs
  - PresentationFormat - intended format of the presentation
  - ScaleCrop - is document scalled or cropped in ThumbnailDisplay Mode
  - SharedDoc - is document "shared"
  - Slides - slides count
  - Template - name of document's template
  - TotalTime - total edit time (in time format)
  - TotalTimeMin - total edit time (in minutes)
  - Words - word count
 
 = Installation =
 
 1. Add Office2007.wdx file as content plugin into your Total Commander instance.
 2. Configure "detection string" for more extensions handling.
 
 
 = Known issues =
 
 Office2007.wdx plugin has been only tested with four default filetypes.
 If you found a bug, please, send me problematic file (if it's possible) attached to e-mail.
 
 
 = Author =
 
 Implemented by Karol Kaminski aka fenixproductions.
 
 fenixproductions at o2 dot pl
 http://fenixproductions.qsh.eu
 
 64-bit build and minor modifications by Konstantin Vlasov
 support at flint-inc dot ru
 http://flint-inc.ru/
 
  
 = Credits =
 
 Office2007.wdx plugin is using:
 
 + tinyXML engine
   - copyright (c) 2000-2006 Lee Thomason (www.grinninglizard.com)
   - http://www.sourceforge.net/projects/tinyxml   
 
 + XUnzip library
 - written by Hans Dietrich
   - http://www.codeproject.com/KB/cpp/xzipunzip.aspx   
 
 
 = Licence =
 
 This plugin is released under The Code Project Open License (CPOL) 1.02.
 
 For more information, please, read "licence.txt" file.
 
 