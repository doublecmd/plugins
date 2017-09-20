#include <cstring>
#include <string>
#include "miniunz.h"
#include "tinyxml.h"
using namespace std;

class OfficeCore
{	
	//string GetField(string xmlstr, char* FieldName);
public:	
	OfficeCore(char*);
	~OfficeCore();
	void GetCore();
	string cp_category;
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
private:
	string GetField(string xmlstr, char* FieldName);
	char* file_name;
};

string Utf8toAnsi(const char * pszCode);
