#include "StdAfx.h"

#include <cstring>
#include <string>
#include "common.h"
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include "miniunz.h"
#include "tinyxml.h"
#include "OfficeApp.h"
using namespace std;

OfficeApp::OfficeApp(char* FileName)
{
	appTemplate = "";
	appManager = "";
	appCompany = "";
	appPages = -1;
	appWords = -1;
	appCharacters = -1;
	appPresentationFormat = "";
	appLines = -1;
	appParagraphs = -1;
	appSlides = -1;
	appNotes = -1;
	appTotalTime = -1;
	appHiddenSlides = -1;
	appMMClips = -1;
	appScaleCrop = "";
	appLinksUpToDate = false;
	appCharactersWithSpaces = -1;
	appSharedDoc = false;
	appHyperlinkBase = "";
	appHyperlinksChanged = false;
	appApplication = "";
	appAppVersion = "";
	appDocSecurity = -1;
	appDigSig = false;
	appDigSigExt = false;
	appSheets = -1;
	file_name = FileName;
}

OfficeApp::~OfficeApp()
{
}

int OfficeApp::StringToInt(string str)
{
	int i;

	if(EOF != sscanf(str.c_str(), "%d", &i))
	{
		return i;
	}

	return -1;
}

bool OfficeApp::StringToBool(string str)
{
	if (str.compare("true") == 0)
	{
		return true;
	}

	return false;
}

bool OfficeApp::StringExistToBool(string str)
{
	if (str.length() > 0)
	{
		return true;
	}

	return false;
}

string OfficeApp::GetField(string xmlstr, char* FieldName)
{
		string tm = "";
		TiXmlDocument* txml = new TiXmlDocument();
		txml->Parse(xmlstr.c_str());

		if (!txml->Error())
		{
			TiXmlHandle hDoc(txml);
			int ind = -1;
			TiXmlElement* element = hDoc.FirstChildElement().ChildElement(FieldName, ind).Element();
			
			if ( element )
			{
				if (element->GetText() != NULL)
				{
					const char* str = element->GetText();

					if (str != NULL)
					{
						tm = Utf8toAnsi(str);
					}
					else
					{
						//tm = "-!-pointer-!-";
						tm = "";
					}
				}
				else
				{
					tm = "";
				}
			}
			else
			{
				//tm = "-!-elem-!-";
				tm = "";
			}
		}
		else
		{
			//tm = "-!-error-!-";
			tm = "";
		}
				
		return tm;
}


string OfficeApp::GetSheets(string xmlstr)
{
		string tm = "";		
		TiXmlDocument* txml = new TiXmlDocument();
		txml->Parse(xmlstr.c_str());

		if (!txml->Error())
		{
			TiXmlHandle hDoc(txml);
			int ind1 = -1;
			int ind2 = -1;
			
			TiXmlElement* element = hDoc.FirstChildElement().ChildElement("TitlesOfParts", ind1).ChildElement("vt:vector", ind2).Element();
			
			if ( element )
			{
				const char* str = element->Attribute("size");

				if (str != NULL)
				{
					tm = string(str);
				}
				else
				{
					//tm = "-!-pointer-!-";
					tm = "";
				}
			}
			else
			{
				//tm = "-!-elem-!-";
				tm = "";
			}			
		}

		return tm;
}

void OfficeApp::GetCore()
{
    unzFile hz;
    string tm = "";

    if ( (hz = unzOpen(file_name) ) != NULL )
    {
        if ((tm = UnzipItem(hz, "docProps/app.xml")).length() > 0)
        {
            //get field
            appTemplate = GetField(tm, "Template");
            appManager = GetField(tm, "Manager");
            appCompany = GetField(tm, "Company");
            appPages = StringToInt(GetField(tm, "Pages"));
            appWords = StringToInt(GetField(tm, "Words"));
            appCharacters = StringToInt(GetField(tm, "Characters"));
            appPresentationFormat = GetField(tm, "PresentationFormat");
            appLines = StringToInt(GetField(tm, "Lines"));
            appParagraphs = StringToInt(GetField(tm, "Paragraphs"));
            appSlides = StringToInt(GetField(tm, "Slides"));
            appNotes = StringToInt(GetField(tm, "Notes"));
            appTotalTime = StringToInt(GetField(tm, "TotalTime"));
            appHiddenSlides = StringToInt(GetField(tm, "HiddenSlides"));
            appMMClips = StringToInt(GetField(tm, "MMClips"));

            if (StringToBool(GetField(tm, "ScaleCrop")))
            {
                appScaleCrop = "Scale";
            }
            else
            {
                appScaleCrop = "Crop";
            }

            appLinksUpToDate = StringToBool(GetField(tm, "LinksUpToDate"));
            appCharactersWithSpaces = StringToInt(GetField(tm, "CharactersWithSpaces"));
            appSharedDoc = StringToBool(GetField(tm, "SharedDoc"));
            appHyperlinkBase = GetField(tm, "HyperlinkBase");
            appHyperlinksChanged = StringToBool(GetField(tm, "HyperlinksChanged"));
            appApplication = GetField(tm, "Application");
            appAppVersion = GetField(tm, "AppVersion");
            appDocSecurity = StringToInt(GetField(tm, "DocSecurity"));

            appDigSig = StringExistToBool(GetField(tm, "DigSig"));
            appDigSigExt = StringExistToBool(GetField(tm, "DigSigExt"));

            if (unzLocateFile(hz, "xl/workbook.xml", 0) == UNZ_OK)
            {
                appSheets = StringToInt(GetSheets(tm));
            }
            else
            {
                appSheets = StringToInt("");
            }
        }
        unzClose(hz);
    }
    else
    {
        appTemplate = "";
        appManager = "";
        appCompany = "";
        appPages = -1;
        appWords = -1;
        appCharacters = -1;
        appPresentationFormat = "";
        appLines = -1;
        appParagraphs = -1;
        appSlides = -1;
        appNotes = -1;
        appTotalTime = -1;
        appHiddenSlides = -1;
        appMMClips = -1;
        appScaleCrop = "";
        appLinksUpToDate = false;
        appCharactersWithSpaces = -1;
        appSharedDoc = false;
        appHyperlinkBase = "";
        appHyperlinksChanged = false;
        appApplication = "";
        appAppVersion = "";
        appDocSecurity = -1;
        appDigSig = false;
        appDigSigExt = false;
        appSheets = -1;
    }
}
