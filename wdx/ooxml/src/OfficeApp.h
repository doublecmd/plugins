#include <cstring>
#include <string>
#include "unzip.h"
#include "tinyxml.h"

using namespace std;

#pragma once

class OfficeApp
{
public:
	OfficeApp(char*);
	~OfficeApp();
	void GetCore();

	string appTemplate;// Name of Document Template
	string appManager;// Name of Manager
	string appCompany;// Name of Company
	int appPages;// Total Number of Pages
	int appWords;// Word Count
	int appCharacters;// Total Number of Characters
	string appPresentationFormat;// Intended Format of Presentation
	int appLines;// Number of Lines
	int appParagraphs;// Total Number of Paragraphs
	int appSlides;// Slides Metadata Element
	int appNotes;// Number of Pages Containing Notes
	int appTotalTime;// Total Edit Time Metadata Element
	int appHiddenSlides;// Number of Hidden Slides
	int appMMClips;// Total Number of Multimedia Clips)
	string appScaleCrop;//1 scale, 0 crop - Thumbnail Display Mode
	bool appLinksUpToDate;//1 updated, 0 outdated - Links Up-to-Date
	int appCharactersWithSpaces;// Number of Characters (With Spaces)
	bool appSharedDoc;// Shared Document
	string appHyperlinkBase;// Relative Hyperlink Base
	bool appHyperlinksChanged;// Hyperlinks Changed
	string appApplication;// Application Name
	string appAppVersion;// Application Version
	int appDocSecurity;// Document Security level
	bool appDigSig;// Digital Signature
	bool appDigSigExt;// Extended Digital Signature
	int appSheets;//number of available sheets
private:	
	string GetField(string xmlstr, char* FieldName);
	string GetSheets(string xmlstr);
	int StringToInt(string str);
	bool StringToBool(string str);
	bool StringExistToBool(string str);
	char* file_name;
};

string Utf8toAnsi(const char * pszCode);
