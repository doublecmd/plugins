#include <cstring>
#include <string>
#include "unzip.h"
#include "tinyxml.h"
using namespace std;

#pragma once

class OfficeFullText
{
public:
	OfficeFullText(char*);
	~OfficeFullText();	
	void GetTitlesText();
	string GetTitlesTextM(int txtstart, int maxlen);
	void GetContentText();		
	void ClearContentText();
	void ClearTitlesText();
	string GetContentTextM(int txtstart, int maxlen);
	string GetContentTextFull();
	string testText;
	bool GetSharedStrings();
private:
	string GetUnformattedText(string unftxt);
	string GetFixedChars(string unftxt);
	string GetWordText();
	string GetExcelText();
	string GetPowerPointText();
	string GetFixedCharsText(string txt);
	string ftContent;
	string ftTitles;	
	string* sharedStrings;
	int sharedSize;
	char* file_name;
	void GetContent();
	void GetTitles();
};
