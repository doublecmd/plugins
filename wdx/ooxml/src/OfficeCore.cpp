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
#include "OfficeCore.h"
using namespace std;

//class OfficeCore
//{
/*string cp_category;
string cp_contentStatus;
string cp_contentType;
string dcterms_created;
string dc_creator;
string dc_description;
string dc_identifier;
string cp_keywords;
string dc_language;
string cp_lastModifiedBy;
string cp_lastPrinted;
string dcterms_modified;
string cp_revision;
string dc_subject;
string dc_title;
string cp_version;
char* file_name;*/

OfficeCore::OfficeCore(char* FileName)
{
	cp_category = "";
	cp_contentStatus = "";
	cp_contentType = "";
	dcterms_created = "";
	dc_creator = "";
	dc_description = "";
	dc_identifier = "";
	cp_keywords = "";
	dc_language = "";
	cp_lastModifiedBy = "";
	cp_lastPrinted = "";
	dcterms_modified = "";
	cp_revision = "";
	dc_subject = "";
	dc_title = "";
	cp_version = "";
	file_name = FileName;
}

string OfficeCore::GetField(string xmlstr, char* FieldName)
{
	string tm = "";
	TiXmlDocument* txml = new TiXmlDocument();
	txml->Parse(xmlstr.c_str());

	if (!txml->Error())
	{
		TiXmlElement* root = txml->FirstChildElement( "cp:coreProperties" );				
		if (root)
		{
			TiXmlElement* element = root->FirstChildElement( FieldName );
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
			//tm = "-!-root-!-";
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

void OfficeCore::GetCore()
{
    string tm = "";

    if ( (tm = UnzipItem(file_name, "docProps/core.xml") ).length() > 0 )
    {
        //get field
        cp_category = GetField(tm, "cp:category");
        cp_contentStatus = GetField(tm, "cp:contentStatus");
        cp_contentType = GetField(tm, "cp:contentType");
        dcterms_created = GetField(tm, "dcterms:created");
        dc_creator = GetField(tm, "dc:creator");
        dc_description = GetField(tm, "dc:description");
        dc_identifier = GetField(tm, "dc:identifier");
        cp_keywords = GetField(tm, "cp:keywords");
        dc_language = GetField(tm, "dc:language");
        cp_lastModifiedBy = GetField(tm, "cp:lastModifiedBy");
        cp_lastPrinted = GetField(tm, "cp:lastPrinted");
        dcterms_modified = GetField(tm, "dcterms:modified");
        cp_revision = GetField(tm, "cp:revision");
        dc_subject = GetField(tm, "dc:subject");
        dc_title = GetField(tm, "dc:title");
        cp_version = GetField(tm, "cp:version");
        /*
            string::size_type loc = dcterms_created.find( "T", 0 );
            if( loc != string::npos )
            {
                dcterms_created.replace(loc, 1, " ");
            }

            loc = dcterms_created.find( "Z", 0 );
            if( loc != string::npos )
            {
                dcterms_created.replace(loc, 1, "");
            }

            dcterms_modified = GetField(tm, "dcterms:modified");

            loc = dcterms_modified.find( "T", 0 );
            if( loc != string::npos )
            {
                dcterms_modified.replace(loc, 1, " ");
            }

            loc = dcterms_modified.find( "Z", 0 );
            if( loc != string::npos )
            {
                dcterms_modified.replace(loc, 1, "");
            }
            */
    }
    else
    {
        cp_category = "";
        cp_contentStatus = "";
        cp_contentType = "";
        dcterms_created = "";
        dc_creator = "";
        dc_description = "";
        dc_identifier = "";
        cp_keywords = "";
        dc_language = "";
        cp_lastModifiedBy = "";
        cp_lastPrinted = "";
        dcterms_modified = "";
        cp_revision = "";
        dc_subject = "";
        dc_title = "";
        cp_version = "";
    }
}

//};
