//
#include "wdxplugin.h"
#include <cstring>
#include <string>
#include <ctime>
#include <iostream>
#include <sstream>
#include "OfficeCore.h"
#include "OfficeApp.h"
#include "OfficeFullText.h"


using namespace std;

#define _detectstring "EXT=\"DOCX\" | EXT=\"XLSX\" | EXT=\"PPTX\" | EXT=\"PPSX\""
#define DATETIME_STRING "%4u-%02u-%02uT%02u:%02u:%02uZ"

typedef struct _field
{
	char *name;
	int type;
	char *unit;
} FIELD;

#define fieldcount (sizeof(fields)/sizeof(FIELD))
int myGlobalPos=0;
char* myGlobalTextBuffer;
BOOL bufferIsReady=false;

#define MAX_BUFF_SIZE 4096*4096

FIELD fields[]={
	{"Category",		ft_string,	""},
	{"ContentStatus",	ft_string,	""},
	{"ContentType",		ft_string,	""},
	//{"Created",			ft_string,	""},
	{"Created",			ft_datetime,	""},
	{"Creator",			ft_string,	""},
	{"Description",		ft_string,	""},
	{"Identifier",		ft_string,	""},
	{"Keywords",		ft_string,	""},
	{"Language",		ft_string,	""},
	{"LastModifiedBy",	ft_string,	""},
	//{"LastPrinted",		ft_string,	""},
	{"LastPrinted",		ft_datetime,	""},
	//{"Modified",		ft_string,	""},
	{"Modified",		ft_datetime,	""},
	{"Revision",		ft_string,	""},
	{"Subject",			ft_string,	""},
	{"Title",			ft_string,	""},
	{"Version",			ft_string,	""},

	{"AppVersion",			ft_string,		""},
	{"Application",			ft_string,		""},
	{"Characters",			ft_numeric_32,	""},
	{"CharactersWithSpaces",ft_numeric_32,	""},
	{"Company",				ft_string,		""},
	{"DigitalSig",			ft_boolean,		""},
	{"DigitalSigExt",		ft_boolean,		""},
	{"DocSecurityLevel",	ft_numeric_32,	""},
	{"HiddenSlides",		ft_numeric_32,	""},
	{"HyperlinkBase",		ft_string,		""},
	{"HyperlinksChanged",	ft_boolean,		""},
	{"Lines",				ft_numeric_32,	""},
	{"LinksUpToDate",		ft_boolean,		""},
	{"MMClips",				ft_numeric_32,	""},
	{"Manager",				ft_string,		""},
	{"Notes",				ft_numeric_32,	""},
	{"Pages",				ft_numeric_32,	""},
	{"Paragraphs",			ft_numeric_32,	""},
	{"PresentationFormat",	ft_string,		""},
	{"ScaleCrop",			ft_string,		""},
	{"SharedDoc",			ft_boolean,		""},
	{"Sheets",				ft_numeric_32,	""},
	{"Slides",				ft_numeric_32,	""},
	{"Template",			ft_string,		""},	
	{"TotalTime",			ft_time,	""},
	{"TotalTimeMin",		ft_numeric_32,	""},	
	{"Words",				ft_numeric_32,	""},	

	//{"TestTitles",			ft_string,	""},
	{"Titles",				ft_fulltext,	""},
	{"Content",				ft_fulltext,	""},
};

BOOL GetValueAborted=false;

char* strlcpy(char* p,const char* p2,int maxlen)
{
    if ((int)strlen(p2)>=maxlen) {
        strncpy(p,p2,maxlen);
        p[maxlen]=0;
    } else
        strcpy(p,p2);
    return p;
}

static BOOL SystemTimeToFileTime(struct tm *lpSystemTime, LPFILETIME lpFileTime)
{
    lpSystemTime->tm_mon -= 1;
    lpSystemTime->tm_isdst = -1;
    lpSystemTime->tm_year -= 1900;
    uint64_t unix_time = mktime(lpSystemTime);
    if (unix_time == (uint64_t)-1) return false;
    unix_time = (unix_time * 10000000) + 0x019DB1DED53E8000;
    lpFileTime->dwLowDateTime = (DWORD)unix_time;
    lpFileTime->dwHighDateTime = unix_time >> 32;
    return true;
}

int DCPCALL ContentGetDetectString(char* DetectString,int maxlen)
{
    strlcpy(DetectString,_detectstring,maxlen);
    return 0;
}

int DCPCALL ContentGetSupportedField(int FieldIndex,char* FieldName,char* Units,int maxlen)
{
    if (FieldIndex<0 || FieldIndex>=fieldcount)
        return ft_nomorefields;
    strncpy(FieldName,fields[FieldIndex].name,maxlen-1);
    strncpy(Units,fields[FieldIndex].unit,maxlen-1);
    return fields[FieldIndex].type;
}

int DCPCALL ContentGetValue(char* FileName,int FieldIndex,int UnitIndex,void* FieldValue,int maxlen,int flags)
{
    string tmpval = "";
    int tmpvalint = -1;
    bool tmpvalbool = false;

    if (access(FileName, F_OK) == 0) {
        OfficeCore* oc = new OfficeCore(FileName);
        oc->GetCore();

        OfficeApp* oa = new OfficeApp(FileName);
        oa->GetCore();
        string tmpbuf = "";

        OfficeFullText* oft = new OfficeFullText(FileName);

        std::tm systime = {0};

        int tmphours = 0;        

        switch (FieldIndex) {
        //CORE
        case 0:  //	cp_category
            tmpval = oc->cp_category;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 1:  //	cp_contentStatus
            tmpval = oc->cp_contentStatus;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 2:  //	cp_contentType
            tmpval = oc->cp_contentType;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 3:  //	dcterms_created
            tmpval = oc->dcterms_created;
            if (6 == sscanf(tmpval.c_str(), DATETIME_STRING, &systime.tm_year, &systime.tm_mon, &systime.tm_mday, &systime.tm_hour, &systime.tm_min, &systime.tm_sec))
            {
                // time zone correction
                SystemTimeToFileTime(&systime, (FILETIME*)FieldValue);
            }
            else
            {
                return ft_fieldempty;
            }
            //strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 4:  //	dc_creator
            tmpval = oc->dc_creator;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 5:  //	dc_description
            tmpval = oc->dc_description;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 6:  //	dc_identifier
            tmpval = oc->dc_identifier;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 7:  //	cp_keywords
            tmpval = oc->cp_keywords;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 8:  //	dc_language
            tmpval = oc->dc_language;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 9:  //	cp_lastModifiedBy
            tmpval = oc->cp_lastModifiedBy;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 10:  // cp_lastPrinted // ft_datetime
            tmpval = oc->cp_lastPrinted;
            if (6 == sscanf(tmpval.c_str(), DATETIME_STRING, &systime.tm_year, &systime.tm_mon, &systime.tm_mday, &systime.tm_hour, &systime.tm_min, &systime.tm_sec))
            {
                // time zone correction
                SystemTimeToFileTime(&systime, (FILETIME*)FieldValue);
            }
            else
            {
                return ft_fieldempty;
            }

            //strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 11:  // dcterms_modified
            tmpval = oc->dcterms_modified;
            if (6 == sscanf(tmpval.c_str(), DATETIME_STRING, &systime.tm_year, &systime.tm_mon, &systime.tm_mday, &systime.tm_hour, &systime.tm_min, &systime.tm_sec))
            {
                // time zone correction
                SystemTimeToFileTime(&systime, (FILETIME*)FieldValue);
            }
            else
            {
                return ft_fieldempty;
            }
            //strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 12:  // cp_revision
            tmpval = oc->cp_revision;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 13:  // dc_subject
            tmpval = oc->dc_subject;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 14:  // dc_title
            tmpval = oc->dc_title;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 15:  // cp_version
            tmpval = oc->cp_version;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
            // APP
        case 16:  // AppVersion,ft_string,
            tmpval = oa->appAppVersion;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 17:  // Application,ft_string,
            tmpval = oa->appApplication;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 18:  // Characters,ft_numeric_32,
            tmpvalint = oa->appCharacters;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 19:  // CharactersWithSpaces,ft_numeric_32,
            tmpvalint = oa->appCharactersWithSpaces;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 20:  // Company,ft_string,
            tmpval = oa->appCompany;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 21:  // DigSig,ft_boolean,
            tmpvalbool = oa->appDigSig;
            if (tmpvalbool)
            {
                *(int*)FieldValue = 1;
            }
            else
            {
                *(int*)FieldValue = 0;
            }
            break;
        case 22:  // DigSigExt,ft_boolean,
            tmpvalbool = oa->appDigSigExt;
            if (tmpvalbool)
            {
                *(int*)FieldValue = 1;
            }
            else
            {
                *(int*)FieldValue = 0;
            }
            break;
        case 23:  // DocSecurityLevel,ft_numeric_32,
            tmpvalint = oa->appDocSecurity;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 24:  // HiddenSlides,ft_numeric_32,
            tmpvalint = oa->appHiddenSlides;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 25:  // HyperlinkBase,ft_string,
            tmpval = oa->appHyperlinkBase;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 26:  // HyperlinksChanged,ft_boolean,
            tmpvalbool = oa->appHyperlinksChanged;
            if (tmpvalbool)
            {
                *(int*)FieldValue = 1;
            }
            else
            {
                *(int*)FieldValue = 0;
            }
            break;
        case 27:  // Lines,ft_numeric_32,
            tmpvalint = oa->appLines;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 28:  // LinksUpToDate,ft_boolean,
            tmpvalbool = oa->appLinksUpToDate;
            if (tmpvalbool)
            {
                *(int*)FieldValue = 1;
            }
            else
            {
                *(int*)FieldValue = 0;
            }
            break;
        case 29:  // MMClips,ft_numeric_32,
            tmpvalint = oa->appMMClips;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 30:  // Manager,ft_string,
            tmpval = oa->appManager;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 31:  // Notes,ft_numeric_32,
            tmpvalint = oa->appNotes;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 32:  // Pages,ft_numeric_32,
            tmpvalint = oa->appPages;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 33:  // Paragraphs,ft_numeric_32,
            tmpvalint = oa->appParagraphs;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 34:  // PresentationFormat,ft_string,
            tmpval = oa->appPresentationFormat;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 35:  // ScaleCrop,ft_string,
            tmpval = oa->appScaleCrop;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 36:  // SharedDoc,ft_boolean,
            tmpvalbool = oa->appSharedDoc;
            if (tmpvalbool)
            {
                *(int*)FieldValue = 1;
            }
            else
            {
                *(int*)FieldValue = 0;
            }
            break;
        case 37:  // Sheets,ft_numeric_32,
            tmpvalint = oa->appSheets;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 38:  // Slides,ft_numeric_32,
            tmpvalint = oa->appSlides;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 39:  // Template,ft_string,
            tmpval = oa->appTemplate;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;
        case 40:  // TotalTime,ft_time
            tmpvalint = oa->appTotalTime;

            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                tmphours = (int) (tmpvalint / 60);
                tmpvalint = tmpvalint - 60*tmphours;
                ((ptimeformat)FieldValue)->wHour=tmphours;
                ((ptimeformat)FieldValue)->wMinute=tmpvalint;
                ((ptimeformat)FieldValue)->wSecond=0;
            }
            break;
        case 41:  // TotalTimeMin,ft_numeric_32,
            tmpvalint = oa->appTotalTime;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
        case 42:  // Words,ft_numeric_32,
            tmpvalint = oa->appWords;
            if (tmpvalint == -1)
            {
                return ft_fieldempty;
            }
            else
            {
                *(int*)FieldValue=tmpvalint;
            }
            break;
            /*case 43://test titles
            oft->GetTitlesText();
            tmpval = oft->testText;
            strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
            break;*/
        case 43:  // Titles, ft_fulltext
            switch(UnitIndex)
            {
            case 0:
                myGlobalPos=0;
                oft->GetTitlesText();
                //initMyTextBuffer();
                //myTextReadingFunction((char*)FieldValue,myGlobalPos,maxlen);
                tmpval = oft->GetTitlesTextM(myGlobalPos, maxlen);
                strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
                myGlobalPos=maxlen;
                break;
            case -1:
                myGlobalPos=0;
                oft->ClearTitlesText();
                tmpval="";
                break;
            default:
                oft->GetTitlesText();
                tmpval = oft->GetTitlesTextM(myGlobalPos, maxlen);

                if (tmpval.length() == 0)
                {
                    return ft_fieldempty;
                }
                else
                {
                    strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
                }
                myGlobalPos=myGlobalPos + maxlen-1;
                break;
            }
            break;
        case 44:  //Content, ft_fulltext,
            switch(UnitIndex)
            {
            case 0:
                myGlobalPos=0;
                oft->GetContentText();
                tmpval = oft->GetContentTextM(myGlobalPos, maxlen);
                strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
                myGlobalPos=maxlen;
                break;
            case -1:
                myGlobalPos=0;
                oft->ClearContentText();
                tmpval="";
                break;
            default:
                oft->GetContentText();
                tmpval = oft->GetContentTextM(myGlobalPos, maxlen);

                if (tmpval.length() == 0)
                {
                    return ft_fieldempty;
                }
                else
                {
                    strlcpy((char*)FieldValue, tmpval.c_str(),maxlen-1);
                }
                myGlobalPos=myGlobalPos + maxlen-1;
                break;
            }
            break;
        default:
            return ft_nosuchfield;
        }
    } else
        return ft_fileerror;
    return fields[FieldIndex].type;  // very important!
}
