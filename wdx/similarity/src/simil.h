#ifdef  __cplusplus
extern "C" {
#endif

#define MAX_LCS 256	/*Maximum size of the longest common sequence. You might wish to change it*/
float simil( char *str1, char *str2);
char *LCS( char *str1, char *str2);

float similRO( char *str1, char *str2);

#ifdef  __cplusplus
}
#endif
