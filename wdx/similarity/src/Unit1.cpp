// filesys.cpp : Defines the entry point for the DLL application.
//

#include "wdxplugin.h"
#include <cstring>
#include <string>
#include <vector>
#include "deelx.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "simil.h"
#include <fcntl.h>
#include <libgen.h>
#include <unistd.h>

using namespace std;

#define _detectstring ""

typedef struct _field
{
	char *name;
	int type;
	char *unit;
} FIELD;

#define fieldcount (sizeof(fields)/sizeof(FIELD))

FIELD fields[]={
	 {"distanceLev",  ft_numeric_32,""},
	 {"similarityLev",   ft_numeric_32,""},
	 {"simil1",   ft_numeric_32,""},
	 {"simil2",   ft_numeric_32,""},
	 {"similRO",   ft_numeric_32,""},
	 {"average",   ft_numeric_32,""},
     {"contains",   ft_numeric_32,""},
};

const char* ConfigFileName = "leven.ini";

char CurrentConfigPath[PATH_MAX];

BOOL GetValueAborted=false;

//REGION  OWN METHODS

/*
bool TestConfigPath(char* path) {
    char* pos = strrchr(path, '\\');
    if (pos != NULL) {
        strcpy(pos + 1, ConfigFileName);
        HANDLE f = CreateFile(
            path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_ALWAYS,
            0,
            NULL);
        if (f != INVALID_HANDLE_VALUE) {
            CloseHandle(f);
            strcpy(CurrentConfigPath, path);
            return true;
        }
    }
    return false;
}

void FindConfigFile(HINSTANCE hDllInstance) {
    char TempPath[MAX_PATH];

    // Try config file in the plugin's dir
    if (GetModuleFileName(hDllInstance, TempPath, MAX_PATH))
        if (TestConfigPath(TempPath))
            return;

    // Next, try TC root dir
    if (GetModuleFileName(NULL, TempPath, MAX_PATH))
        if (TestConfigPath(TempPath))
            return;

    // And, finally, use wincmd.ini location
    if (GetEnvironmentVariable("COMMANDER_INI", TempPath, MAX_PATH))
        if (TestConfigPath(TempPath))
            return;
}
*/

//int __stdcall min(int x, int y) {
//	if (x <y)
//	{
//		return x;
//	}
//
//	return y;
//}

int ComputeDistance(const char* source, const char* target){

	// Step 1

	const int n = strlen(source);
	const int m = strlen(target);
	
	if (n == 0){
		return m;
	}
	
	if (m == 0){
    	return n;
	}

	// Good form to declare a TYPEDEF

	typedef std::vector< std::vector<int> > Tmatrix; 

	Tmatrix matrix(n+1);

	// Size the vectors in the 2.nd dimension. Unfortunately C++ doesn't
	// allow for allocation on declaration of 2.nd dimension of vec of vec

	for (int i = 0; i <= n; i++){
		matrix[i].resize(m+1);
	}

	// Step 2

	for (int i = 0; i <= n; i++){
		matrix[i][0]=i;
	}

	for (int j = 0; j <= m; j++){
		matrix[0][j]=j;
	}

	// Step 3

	for (int i = 1; i <= n; i++){

		const char s_i = source[i-1];

		// Step 4

		for (int j = 1; j <= m; j++){
			
			const char t_j = target[j-1];
			
			// Step 5
			
			int cost;
			
			if (s_i == t_j){
				cost = 0;
			}
			else{
				cost = 1;
			}

			// Step 6

			const int above = matrix[i-1][j];
			const int left = matrix[i][j-1];
			const int diag = matrix[i-1][j-1];
			int cell = min( above + 1, min(left + 1, diag + cost));

			// Step 6A: Cover transposition, in addition to deletion,
			// insertion and substitution. This step is taken from:
			// Berghel, Hal ; Roach, David : "An Extension of Ukkonen's 
			// Enhanced Dynamic Programming ASM Algorithm"
			// (http://www.acm.org/~hlb/publications/asm/asm.html)

			if (i>2 && j>2){
				int trans=matrix[i-2][j-2]+1;
				if (source[i-2]!=t_j) trans++;
				if (s_i!=target[j-2]) trans++;
				if (cell>trans) cell=trans;
			}
			
			matrix[i][j]=cell;
		}
	}
	
	// Step 7
	
	return matrix[n][m];
}


int GetSimilarity( char* string1, char* string2 ){

	float dis = ComputeDistance( string1, string2 );
	float maxLen = strlen(string1);
	
	if (maxLen < (float) strlen(string2))
		maxLen = (float) strlen(string2);
	
	if (maxLen == 0.0F)
		return 100;
	else
		return (int) (100.0 * (1.0F - dis / maxLen));
}

int Contains(char* str, char* substr)
{
	return (strstr(str, substr) == NULL) ? 0 : 1;
}

//ENDREGION OWN METHODS

int DCPCALL ContentGetDetectString(char* DetectString,int maxlen){
	
	strncpy(DetectString,_detectstring,maxlen);
	return 0;
}

int DCPCALL ContentGetSupportedField(int FieldIndex,char* FieldName,char* Units,int maxlen){

	if (FieldIndex<0 || FieldIndex>=fieldcount)
		return ft_nomorefields;
	
	strncpy(FieldName,fields[FieldIndex].name,maxlen-1);
	
	strncpy(Units,fields[FieldIndex].unit,maxlen-1);
	
	return fields[FieldIndex].type;
}

int DCPCALL ContentGetValue(char* FileName,int FieldIndex,int UnitIndex,void* FieldValue,int maxlen,int flags){

    char *cFileName;

    GetValueAborted=false;

	if (flags & CONTENT_DELAYIFSLOW){
		if (FieldIndex==7)
			return ft_delayed;
		if (FieldIndex==8)
			return ft_ondemand;
	}

    if (access(FileName, F_OK) == 0){

        string line;
        cFileName = basename(FileName);
  		ifstream myfile (CurrentConfigPath);
        static CRegexpT <char> regexp("Phrase=(.*)", IGNORECASE | MULTILINE);

        char val[MAX_PATH];
        if (myfile.is_open()){

        	strcpy(val, "");

			while (! myfile.eof() ){

	            getline (myfile,line);
            	MatchResult result = regexp.Match(line.c_str());

				if( result.IsMatched() ){
                	strncpy(val, line.c_str() + result.GetGroupStart(1), result.GetGroupEnd(1) - result.GetGroupStart(1));
                }

            }

            myfile.close();
        }

        float aver = 0.0f;

		switch (FieldIndex){
			case 0:  //	Levenshtein distance
                    *(int*)FieldValue=ComputeDistance(val, cFileName );
					break;
			case 1:  // Similarity using Levenshtein distance
                    *(int*)FieldValue=GetSimilarity(val, cFileName );
					break;
            case 2://
                    *(int*)FieldValue=(int) 100*simil(val, cFileName );
		            break;
            case 3:
                    *(int*)FieldValue=(int) 100*simil(cFileName, val );
                    break;
			case 4:
                    *(int*)FieldValue=(int) 100*similRO(val, cFileName );
                    break;
			case 5:
                    aver = GetSimilarity(val, cFileName ) + 100*(simil(val, cFileName ) +
                        simil(cFileName, val ) + similRO(val, cFileName ));

                    aver /= 4;

                    if (aver <= 100)
                    {
                        aver += 50 * Contains(cFileName, val);
                    }

                    if (aver >= 100)
                    {
                    	aver = 100;
                    }

		            *(int*)FieldValue=(int) aver;
                    break;
            case 6:
                    *(int*)FieldValue=Contains(cFileName, val );
            		break;
			default:
					return ft_nosuchfield;
		}
    } else
    	return ft_fileerror;

	return fields[FieldIndex].type;  // very important!
}

void DCPCALL ContentSetDefaultParams(ContentDefaultParamStruct* dps){

    strcpy(CurrentConfigPath, dps->DefaultIniName);
    char* pos = strrchr(CurrentConfigPath, '/');
    if (pos != NULL) {
        strcpy(pos + 1, ConfigFileName);
    }
}
