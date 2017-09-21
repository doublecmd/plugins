/*	Coded by Rui A. Rebelo - rui@despammed.com
	12/September/2004 - Version 1.0
*/
/* simil.c
	A module with a function to return similarity between 2 strings
	and another one which returns one of the longest comon subsequences
	in both strings.
*/
#ifndef _INC_STRING
#include	<string.h>
#endif

#ifndef _INC_STDLIB
#include	<stdlib.h>
#endif

#include "simil.h"
/* Quick and dirty swap of the address of 2 arrays of unsigned int
*/
void swap( unsigned **first, unsigned **second)
{
	unsigned *temp;
	temp=*first;
	*first=*second;
	*second=temp;
}
/* A function which returns how similar 2 strings are
   Assumes that both point to 2 valid null terminated array of chars.
   Returns the similarity between them.
*/
float simil( char *str1, char *str2)
{
	size_t len1=strlen(str1), len2=strlen(str2);
	float lenLCS;
	unsigned j, k, *previous, *next;
	if( len1==0 || len2==0)
		return 0;
	previous=(unsigned *)calloc( len1+1, sizeof(unsigned));
	next=(unsigned *)calloc( len1+1, sizeof(unsigned));
	for(j=0; j<len2; ++j){
		for(k=1; k<=len1; ++k)
			if( str1[k-1] == str2[j])
				next[k]=previous[k-1]+1;
			else next[k]=previous[k]>=next[k-1]?previous[k]:next[k-1];
		swap( &previous, &next);
	}
	lenLCS=(float)previous[len1];
	free(previous);
	free(next);
	return lenLCS/=len1;
}
/*	Returns a pointer to the Longest Common Sequence in str1 and str2
	Assumes str1 and str2 point to 2 null terminated array of char
*/
char *LCS( char *str1, char *str2)
{
  static char lcs[MAX_LCS];
  int i, r, c, len1=(int)strlen(str1), len2=(int)strlen(str2);
  unsigned **align;
  if( len1==0 || len2==0)
    return 0;
  align=(unsigned **)calloc( len2+1, sizeof(unsigned *));
  for( r=0; r<=len2; ++r)
	  align[r]=(unsigned *)calloc( len1+1, sizeof(unsigned));
  for(r=1; r<=len2; ++r)
	  for(c=1; c<=len1; ++c)
		  if( str1[c-1] == str2[r-1])
			  align[r][c]=align[r-1][c-1]+1;
		  else align[r][c]=align[r-1][c]>=align[r][c-1]?align[r-1][c]:align[r][c-1];
  for(r=len2, c=len1,i=align[r][c], lcs[i]='\0';
	  i>0 && r>0 && c>0; i=align[r][c]){
		  if( align[r-1][c] ==i)
			  --r;
		  else if( align[r][c-1]==i)
			  --c;
		  else if(align[r-1][c-1]==i-1){
			  lcs[i-1]=str2[--r];
			  --c;
		  }
  }
  for( r=len2;r>=0; --r)
	  free( align[r]);
  free(align);
  return lcs;
}

/* A function which returns how similar 2 strings are
   Assumes that both point to 2 valid null terminated array of chars.
   Returns the similarity between them.
   MODIFIED: for Ratcliff/Obershelp pattern recognition
*/
float similRO( char *str1, char *str2)
{
	size_t len1=strlen(str1), len2=strlen(str2);
	float lenLCS;
	unsigned j, k, *previous, *next;
	if( len1==0 || len2==0)
		return 0;
	previous=(unsigned *)calloc( len1+1, sizeof(unsigned));
	next=(unsigned *)calloc( len1+1, sizeof(unsigned));
	for(j=0; j<len2; ++j){
		for(k=1; k<=len1; ++k)
			if( str1[k-1] == str2[j])
				next[k]=previous[k-1]+1;
			else next[k]=previous[k]>=next[k-1]?previous[k]:next[k-1];
		swap( &previous, &next);
	}
	lenLCS=(float)previous[len1];
	free(previous);
	free(next);
	return lenLCS*=2.0/(len1+len2);
}