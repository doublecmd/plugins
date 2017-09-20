#include "common.h"
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iterator>

#include "miniunz.h"
#include "tinyxml.h"
#include "OfficeFullText.h"
#include "OfficeApp.h"

using namespace std;

OfficeFullText::OfficeFullText(char* FileName)
{
	ftTitles = "";
	ftContent = "";
	testText = "";
	file_name = FileName;
	sharedSize = -1;
}

OfficeFullText::~OfficeFullText()
{
}

string OfficeFullText::GetUnformattedText(string frmtxt)
{
		bool isInside = false;
		string tm2 = "";
		char prev = '\0';

		for (int i = 0; i < frmtxt.length(); i++)
		{
			if (frmtxt[i] == '<')
			{
				isInside = true;
			}

			if (frmtxt[i] == '>')
			{
				isInside = false;
			}

			if (!isInside && (frmtxt[i] != '<'))
			{
				if  (frmtxt[i] == '>')
				{
					if (prev != ' ')
					{
						tm2 = tm2;// + " ";
					}
				}
				else
				{
					int znak = (int) frmtxt[i];

					if ( znak >=0  && znak <= 31 )
					{
						//tm2 = tm2 + " ";
					}
					else
					{
						tm2 = tm2 + frmtxt[i];
					}
				}

				//prev = unftxt[i];
				if (tm2.length() > 0)
				{
					if (tm2[tm2.length() - 1] != NULL)
					{
						prev = tm2[tm2.length() - 1];
					}
					else
					{
						prev = ' ';
					}
				}
			}
		}
		
		if (strlen(tm2.c_str()) == 0)
		{
			tm2 = " ";
		}

		return tm2;
}


string GetUnformattedTextPP(string frmtxt)
{
		bool isInside = false;
		string tm2 = "";
		char prev = '\0';

		for (int i = 0; i < frmtxt.length(); i++)
		{
			if (frmtxt[i] == '<')
			{
				isInside = true;
			}

			if (frmtxt[i] == '>')
			{
				isInside = false;
			}

			if (!isInside && (frmtxt[i] != '<'))
			{
				if  (frmtxt[i] == '>')
				{
					if (prev != ' ')
					{
						tm2 = tm2;
					}
				}
				else
				{
					int znak = (int) frmtxt[i];					
					
					if ( znak >=0  && znak <= 31 )
					{
						tm2 = tm2 + " ";
					}
					else
					{
						tm2 = tm2 + frmtxt[i];						
					}					
				}

				if (tm2.length() > 0)
				{
					prev = tm2[tm2.length() - 1];
				}
			}
		}

		if (strlen(tm2.c_str()) == 0)
		{
			return " ";
		}

		return tm2;
}

string OfficeFullText::GetFixedChars(string frmtxt)
{		
		string tm2 = "";

		for (int i = 0; i < frmtxt.length(); i++)
		{
			int znak = (int) frmtxt[i];

			if ( znak >=0  && znak <= 31 )
			{
				tm2 = tm2 + " ";
			}
			else
			{
				tm2 = tm2 + frmtxt[i];
			}
		}

		return tm2;
}

void OfficeFullText::GetContentText()
{
	GetContent();	
}

string OfficeFullText::GetContentTextM(int txtstart, int maxlen)
{
	string tmp3 = "";

	int txtend = txtstart + maxlen;

	if (txtend > ftContent.length())
	{
		maxlen = ftContent.length() - txtstart;
	}

	if ( (maxlen <= 0) || (txtstart >= ftContent.length()) )
	{			
		return "";
	}

	char* buf;
	buf = new char[maxlen];

	ftContent.copy(buf, maxlen, txtstart);
	
	string tmp2 = (string) buf;	

	tmp2 = tmp2 + '\0';
	
	return tmp2;
}

void OfficeFullText::GetContent()
{
    unzFile hz;

    if ( (hz = unzOpen(file_name) ) != NULL )
    {
        int type = -1;
        //0 word, 1 excel, 2 ppoint

        if (unzLocateFile(hz, "word/document.xml", 0) == UNZ_OK)
        {
            type = 0;
        }
        else
        {
            if (unzLocateFile(hz, "xl/workbook.xml", 0) == UNZ_OK)
            {
                type = 1;
            }
            else
            {
                if (unzLocateFile(hz, "ppt/presentation.xml", 0) == UNZ_OK)
                {
                    type = 2;
                }
                else
                {
                    type = -1;
                }
            }
        }

        switch (type)
        {
        case 0://word
            testText = GetWordText();
            break;
        case 1://excel
            testText = GetExcelText();
            break;
        case 2://ppt
            testText = GetPowerPointText();
            break;
        default:
            testText = "";
            break;
        }

        unzClose(hz);
    }
    else
    {
        testText = "";
    }

    ftContent = testText;
}

string replace_all_copy(const string& haystack, const string& needle, const string& replacement) {
    if (needle.empty()) {
        return haystack;
    }
    string result;

    for (string::const_iterator substart = haystack.begin(), subend; ; ) {
        subend = search(substart, haystack.end(), needle.begin(), needle.end());
        copy(substart, subend, back_inserter(result));
        if (subend == haystack.end()) {
            break;
        }
        copy(replacement.begin(), replacement.end(), back_inserter(result));
        substart = subend + needle.size();
    }
    return result;
}

string OfficeFullText:: GetFixedCharsText(string txt)
{	
	//xml forbidden characters
	txt = replace_all_copy(txt, "&amp;", "&");
	txt = replace_all_copy(txt, "&lt;", "<");
	txt = replace_all_copy(txt, "&gt;", ">");
	txt = replace_all_copy(txt, "&apos;", "\\");
	txt = replace_all_copy(txt, "&quot;", "\"");

	//text characters
	//txt = replace_all_copy(txt, "\r\n", "%%");
	//txt = replace_all_copy(txt, "\t", "$$");
	return txt;
}

string Utf8toAnsi(const char * pszCode)
{
    string res = pszCode;

	return res;
}

string OfficeFullText::GetWordText()
{
    string tm = "";
    string outp = "";
    string tmpval = "";
    bool isnewline = false;

    if ((tm = UnzipItem(file_name, "word/document.xml")).length() > 0)
    {
        //xml
        TiXmlDocument* txml = new TiXmlDocument();

        txml->Parse(tm.c_str());

        if (!txml->Error())
        {
            TiXmlHandle hDoc(txml);
            int ind1 = -1;

            TiXmlElement* root = hDoc.FirstChildElement( "w:document" ).ChildElement("w:body", ind1).Element();

            if (root)
            {
                TiXmlElement* elem = root->FirstChildElement();

                while (elem)//w:p
                {
                    TiXmlElement* subelem = elem->FirstChildElement();

                    while(subelem)//w:r
                    {
                        TiXmlElement* subsubelem = subelem->FirstChildElement();

                        while(subsubelem)//w:t looking
                            //if(subsubelem)
                        {

                            isnewline = true;

                            const char* str2;

                            tmpval = subsubelem->Value();

                            if (tmpval.compare("w:t") == 0)
                            {
                                if (subsubelem->GetText() != NULL)
                                {
                                    str2 = subsubelem->GetText();

                                    outp = outp + Utf8toAnsi(subsubelem->GetText()) + "\t";
                                }
                                else
                                {
                                    outp = outp + " " + "\t";
                                }
                            }

                            subsubelem = subsubelem->NextSiblingElement();
                        }

                        subelem = subelem->NextSiblingElement();
                    }
                    if (isnewline)
                    {
                        outp = outp + "\n";
                        isnewline = false;
                    }
                    elem = elem->NextSiblingElement();
                }
            }
        }
    }

    //TODO: GetFixedChars!

    if (outp.length() == 0)
    {
        outp = " ";
    }

    return outp;
}


int StringToInt(string str)
{
	int i;

	if (str.length() > 0)
	{
		if(EOF != sscanf(str.c_str(), "%d", &i))
		{
			return i;
		}
	}

	return -1;
}


bool OfficeFullText::GetSharedStrings()
{
    //get from xl\sharedStrings.xml
    string tm = "";
    string tmval = "";
    string tmpansi = "";
    bool out = false;

    if ((tm = UnzipItem(file_name, "xl/sharedStrings.xml")).length() > 0)
    {
        //get strings count
        TiXmlDocument* txml = new TiXmlDocument();
        txml->Parse(tm.c_str());

        if (!txml->Error())
        {
            TiXmlHandle hDoc(txml);
            TiXmlElement* root = txml->FirstChildElement( "sst" );

            if ( root != NULL )
            {
                const char* tmpcount = root->Attribute("uniqueCount");

                if (tmpcount != NULL)
                {
                    int count = StringToInt(tmpcount);

                    if(count > 0)
                    {
                        //return count;
                        sharedSize = count;
                        sharedStrings = new string[count];

                        TiXmlElement* sielem = root->FirstChildElement();
                        int i = 0;

                        while( sielem )
                        {
                            //read string
                            TiXmlElement* tsielem = sielem->FirstChildElement();

                            //TOFO: get text from formatted nodes "r"
                            if (tsielem != NULL)
                            {
                                //LogToFile2((string) tsielem->Value());
                                tmval = (string) tsielem->Value();

                                if (tmval.compare("t") == 0)
                                {
                                    if (tsielem->GetText() != NULL)
                                    {
                                        //LogToFile2(tsielem->GetText());
                                        tmpansi = (string) tsielem->GetText();
                                        tmpansi = Utf8toAnsi(tmpansi.c_str());
                                        sharedStrings[i] = tmpansi;
                                    }
                                    else
                                    {
                                        //LogToFile2(" ");
                                        sharedStrings[i] = " ";
                                    }

                                    i++;
                                }
                            }

                            sielem = sielem->NextSiblingElement();
                        }

                        out = true;
                    }
                }
            }
        }
    }

    return out;
}


string OfficeFullText::GetExcelText()
{
    string tm = "";
    string tmb = "";
    string tmval = "";
    string tmpget = "";
    string tmpatr = "";
    string tmpansi = "";
    unzFile hz;

    OfficeApp* oa;
    int slides = 0;
    string slidename;
    int j = 0;
    string outp = "";
    bool isnewline = false;
    //LogToFile2("szered");
    bool hasShared = GetSharedStrings();

    if ( (hz = unzOpen(file_name) ) != NULL )
    {
        if (unzLocateFile(hz, "xl/workbook.xml", 0) == UNZ_OK)
        {
            oa = new OfficeApp(file_name);
            oa->GetCore();
            slides = oa->appSheets;

            if (slides > 0)
            {
                tm = "";
                outp = "";

                for (j = 1; j <= slides; j++)
                {
                    slidename = std::to_string(j);

                    slidename = "xl/worksheets/sheet" + slidename + ".xml";

                    if ((tmb = UnzipItem(hz, slidename.c_str())).length() > 0)
                    {
                        //xml
                        TiXmlDocument* txml = new TiXmlDocument();
                        txml->Parse(tmb.c_str());

                        if (!txml->Error())
                        {
                            TiXmlHandle hDoc(txml);
                            int ind1 = -1;

                            TiXmlElement* root = hDoc.FirstChildElement( "worksheet" ).ChildElement("sheetData", ind1).Element();
                            if (root)
                            {
                                TiXmlElement* elem = root->FirstChildElement();

                                while (elem)//rows
                                {
                                    TiXmlElement* subelem = elem->FirstChildElement();

                                    while(subelem)//columns
                                    {
                                        isnewline = true;

                                        TiXmlElement* colval = subelem->FirstChildElement();

                                        if (subelem->Attribute("t") != NULL)
                                        {
                                            tmpatr = (string) subelem->Attribute("t");
                                        }
                                        else
                                        {
                                            tmpatr = " ";
                                        }

                                        while(colval)
                                        {
                                            //
                                            tmval = (string) colval->Value();

                                            if ((tmval.compare("is") == 0) && (tmpatr.compare("inlineStr") == 0))
                                            {
                                                TiXmlElement* inlineelem = colval->FirstChildElement();
                                                //

                                                if (inlineelem)//is -> t
                                                {
                                                    tmval = inlineelem->Value();

                                                    if ( (tmval.compare("t") == 0) && (inlineelem->GetText() != NULL))
                                                    {
                                                        tmpansi =  Utf8toAnsi(inlineelem->GetText());
                                                        outp = outp + tmpansi + "\t";
                                                    }
                                                    else
                                                    {
                                                        outp = outp + " " + "\t";
                                                    }
                                                }
                                                else
                                                {
                                                    outp = outp + " " + "\t";
                                                }
                                            }
                                            else
                                            {
                                                if ((tmval.compare("v") == 0) && (tmpatr.compare("s") == 0))
                                                {
                                                    if (hasShared)
                                                    {
                                                        if (colval->GetText() != NULL)
                                                        {
                                                            int nr = StringToInt((string) colval->GetText());

                                                            if ((nr > -1) && (nr < sharedSize) )
                                                            {
                                                                outp = outp + sharedStrings[nr] + "\t";
                                                            }
                                                            else
                                                            {
                                                                outp = outp + " " + "\t";
                                                            }
                                                        }
                                                        else
                                                        {
                                                            outp = outp + " " + "\t";
                                                        }
                                                    }
                                                }
                                                else
                                                {
                                                    if ((tmval.compare("v") == 0) && (colval->GetText() != NULL))
                                                    {
                                                        outp = outp + Utf8toAnsi(colval->GetText()) + "\t";
                                                    }
                                                    else
                                                    {
                                                        outp = outp + " " + "\t";
                                                    }
                                                }
                                            }

                                            colval = colval->NextSiblingElement();
                                        }

                                        subelem = subelem->NextSiblingElement();
                                    }
                                    elem = elem->NextSiblingElement();

                                    if (isnewline)
                                    {
                                        outp = outp + "\n";
                                        isnewline = false;
                                    }
                                }
                            }
                        }

                    }

                    outp = outp + "\n\n";

                }
            }
            else
            {
                outp = "";
            }

            unzClose(hz);
        }
        else
        {
            outp = "";
        }
    }

    if (outp.length() == 0)
    {
        outp = " ";
    }

    //TODO: GetFixedChars!

    return outp;
}

string OfficeFullText::GetPowerPointText()
{	
    string tm = "";
    string tmb = "";
    unzFile hz;

    OfficeApp* oa;
    int slides = 0;
    string slidename;
    int j = 0;
    string outp = "";
    bool isnewline = false;

    if ( (hz = unzOpen(file_name) ) != NULL )
    {
        if (unzLocateFile(hz, "ppt/presentation.xml", 0) == UNZ_OK)
        {
            oa = new OfficeApp(file_name);
            oa->GetCore();
            slides = oa->appSlides;

            if (slides > 0)
            {
                tm = "";
                outp = "";

                for (j = 1; j <= slides; j++)
                {
                    slidename = std::to_string(j);

                    slidename = "ppt/slides/slide" + slidename + ".xml";

                    if ((tmb = UnzipItem(hz, slidename.c_str())).length() > 0)
                    {
                        //xml
                        TiXmlDocument* txml = new TiXmlDocument();
                        txml->Parse(tmb.c_str());

                        if (!txml->Error())
                        {
                            TiXmlHandle hDoc(txml);
                            int ind1 = -1;

                            TiXmlElement* root = hDoc.FirstChildElement( "p:sld" ).ChildElement("p:cSld", ind1).ChildElement("p:spTree", ind1).Element();

                            if (root)
                            {
                                TiXmlElement* elem = root->FirstChildElement();

                                while (elem)//p:sp
                                {
                                    TiXmlElement* subelem = elem->FirstChildElement();

                                    while(subelem)
                                    {
                                        TiXmlElement* subsubelem = subelem->FirstChildElement();

                                        while(subsubelem)
                                        {
                                            isnewline = true;
                                            TiXmlPrinter printer;
                                            printer.SetIndent(NULL);
                                            subsubelem->Accept(&printer);

                                            string tmpstr3 = printer.CStr();

                                            string tmpstr4 = Utf8toAnsi(tmpstr3.c_str());
                                            outp = outp + GetUnformattedTextPP(tmpstr4) + " ";//"\t";

                                            //string tmpstr4 = GetUnformattedTextPP(tmpstr3);

                                            //const char* str3 = tmpstr4.c_str();
                                            //outp = outp + Utf8toAnsi(str3) + " ";//"\t";

                                            subsubelem = subsubelem->NextSiblingElement();
                                        }
                                        subelem = subelem->NextSiblingElement();

                                        if (isnewline)
                                        {
                                            outp = outp + "\n";
                                            isnewline = false;
                                        }
                                    }
                                    elem = elem->NextSiblingElement();
                                }
                            }

                        }
                    }
                }
            }
            else
            {
                outp = "";
            }

            unzClose(hz);
        }
        else
        {
            outp = "";
        }
    }

    if (outp.length() == 0)
    {
        outp = " ";
    }

    //TODO: GetFixedChars

    return outp;
}


void OfficeFullText::GetTitles()
{
    string tm = "";
    string tmb = "";
    string tmpansi = "";
    // int k = 0;

    string outp = "";

    if ( (tmb = UnzipItem(file_name, "docProps/app.xml") ).length() > 0 )
    {
        TiXmlDocument* txml = new TiXmlDocument();
        txml->Parse(tmb.c_str());

        if (!txml->Error())
        {
            TiXmlHandle hDoc(txml);
            int ind1 = -1;
            int ind2 = -1;

            TiXmlElement* root = hDoc.FirstChildElement( "Properties" ).ChildElement("TitlesOfParts", ind1).ChildElement("vt:vector", ind2).Element();

            if (root)
            {
                TiXmlElement* elem = root->FirstChildElement();

                while (elem)
                {
                    const char* str2;

                    if (elem->GetText() != NULL)
                    {
                        str2 = elem->GetText();

                        // k++;
                        // char buforek[33];
                        // _itoa(k, buforek, 10);

                        tmpansi = Utf8toAnsi(str2);

                        tm = tm + " " + tmpansi;
                    }

                    elem = elem->NextSiblingElement();
                }
            }
        }

        outp = tm;
        //outp = GetFixedChars(outp);

        outp = GetFixedCharsText(outp);

        outp = outp + '\0';
        tm = "";
    }

    ftTitles = outp;
}

void OfficeFullText::GetTitlesText()
{
	GetTitles();	
	//testText= ftTitles;
}

void OfficeFullText::ClearTitlesText()
{
	ftTitles = "";
}

void OfficeFullText::ClearContentText()
{
	ftContent = "";
}

string OfficeFullText::GetTitlesTextM(int txtstart, int maxlen)
{	
	string tmp3 = "";

	int txtend = txtstart + maxlen;

	if (txtend > ftTitles.length())
	{
		maxlen = ftTitles.length() - txtstart;
	}

	if ( (maxlen <= 0) || (txtstart >= ftTitles.length()) )
	{			
		return "";
	}

	char* buf;
	buf = new char[maxlen];

	ftTitles.copy(buf, maxlen, txtstart);
	
	string tmp2 = (string) buf;
	

	tmp2 = tmp2 + '\0';

	for (int i = 0; i < maxlen; i++)
	{
		tmp3 = tmp3 + tmp2[i];
	}

	tmp3 = tmp3 + '\0';	

	return tmp3;
}
