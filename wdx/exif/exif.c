// exif.cpp : Defines the entry point for the DLL application.
//

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include "wdxplugin.h"

#define byte uint8_t
#define INVALID_HANDLE_VALUE -1
#define min(a, b) (((a) < (b)) ? (a) : (b))

#define COUNTOF(x) sizeof(x)/sizeof(x[0])

#define _detectstring "(EXT=\"JPG\") | (EXT=\"JPEG\") | (EXT=\"TIFF\") | (EXT=\"TIF\") | (EXT=\"JPE\") | (EXT=\"CRW\") | (EXT=\"THM\") | (EXT=\"CR2\") | (EXT=\"CR3\") | (EXT=\"DNG\") | (EXT=\"NEF\") | (EXT=\"ORF\") | (EXT=\"RAW\") | (EXT=\"RW2\") | (EXT=\"ARW\") | (EXT=\"PEF\") | (EXT=\"RAF\")"

BOOL ParseTiffFile(int f,char* data,int datalen,int TagNeeded,int TagNeeded2,int FormatNeeded,int FieldIndex, void* FieldValue,int maxlen,BOOL unicode,int UnitIndex);

#define fieldcount 79

#define canon_start 50
#define canon_end 72

#define gps_start 73
#define gps_end 78

char* fieldnames[fieldcount]={"Width","Height","BitsPerSample","DateTimeStr",
	"Date","Time","DateOriginal","TimeOriginal","DateDigitized","TimeDigitized",
	"ComponentsConfiguration","CompressedBitsPerPixel",
	"ExifVersion","ExposureBiasValue","ExposureProgram","ExposureTimeFraction","ExposureTimeNr","Flash",
	"FNumber","FocalLength","ISO","LightSource", "Make","ApertureValue","MaxApertureValue",
	"MeteringMode","Model","Orientation",
	// Added values after version 1.4
	"ImageDescription","UserComment","XResolution","YResolution","ResolutionUnit","Software","Artist",
	"ShutterSpeed","YCbCrPositioning","SensingMethod","ExposureMode","WhiteBalance","DigitalZoomRatio",
	"FocalLengthIn35mmFilm","SceneCaptureType","GainControl","Contrast","Saturation","Sharpness","SubjectDistanceRange",
	// Added values after version 2.5
	"Compression","CompressionName",
	// Canon MakerNote Fields
	"Canon Macro mode","Canon Flash mode","Canon Continuous drive mode",
	"Canon Focus Mode","Canon Image size","Canon Easy shooting mode","Canon Digital Zoom",
	"Canon Contrast","Canon Saturation","Canon Sharpness",
	"Canon ISO Speed","Canon Metering Mode","Canon Focus Type","Canon AF point selected","Canon Exposure mode",
	"Canon Flash Activity","Canon White Balance","Canon Flash Bias","Canon Image type","Canon Firmware version",
	"Canon Image number","Canon Owner name","Canon Camera serial number",
	"GPS Latitude","GPS Longitude","GPS Altitude","GPS Timestamp", "GPS Direction", "GPS Datestamp"
	};

int fieldtypes[fieldcount]={ft_numeric_32,ft_numeric_32,ft_numeric_32,ft_string,
	ft_date,ft_time,ft_date,ft_time,ft_date,ft_time,
	ft_string,ft_numeric_floating,
	ft_string,ft_numeric_floating,ft_multiplechoice,ft_string,ft_numeric_floating,ft_multiplechoice,
	ft_numeric_floating,ft_numeric_floating,ft_numeric_32,ft_multiplechoice,ft_string,ft_numeric_floating,ft_numeric_floating,   // The last two where update from ft_string after 1.4
	ft_multiplechoice,ft_string,ft_multiplechoice,
	// Added values after version 1.4
	ft_string,ft_string,ft_numeric_floating,ft_numeric_floating,ft_multiplechoice,ft_string,ft_string,
	ft_string,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_numeric_floating,
	ft_numeric_32,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,
	// Added values after version 2.5
	ft_numeric_32,ft_multiplechoice,
	// Canon MakerNote Fields
	ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,
	ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,
	ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,
	ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,
	ft_multiplechoice,ft_multiplechoice,ft_multiplechoice,ft_string,ft_string,
	ft_string,ft_string,ft_string,
	ft_numeric_floating,ft_numeric_floating,ft_numeric_floating,ft_time,ft_numeric_floating,ft_date
	};

int tagtable[fieldcount]={0xA002,0xA003,0x102,0x132,
	0x132,0x132,0x9003,0x9003,0x9004,0x9004,
	0x9101,0x9102,
	0x9000,0x9204,0x8822,0x829A,0x829A,0x9209,
	0x829D,0x920A,0x8827,0x9208,0x10F,0x9202,0x9205,
	0x9207,0x110,0x112,
	// Added values after version 1.4
	0x010E,0x9286,0x11A,0x11B,0x128,0x131,0x13B,
	0x9201,0x213,0xA217,0xA402,0xA403,0xA404,
	0xA405,0xA406,0xA407,0xA408,0xA409,0xA40A,0xA40C,
	// Added values after version 2.5
	0x103,0x103,
	// Canon MakerNote Fields
	0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,

	0x8825,0x8825,0x8825,0x8825,0x8825,0x8825
	};

int alternatetagtable[fieldcount]={0x100,0x101,0x102,0x132,
	0x132,0x132,0x9003,0x9003,0x9004,0x9004,
	0x9101,0x9102,
	0x9000,0x9204,0x8822,0x829A,0x829A,0x9209,
	0x829D,0x920A,0x8827,0x9208,0x10F,0x9202,0x9205,
	0x9207,0x110,0x112,
	// Added values after version 1.4
	0x010E,0x9286,0x11A,0x11B,0x128,0x131,0x13B,
	0x9201,0x213,0xA217,0xA402,0xA403,0xA404,
	0xA405,0xA406,0xA407,0xA408,0xA409,0xA40A,0xA40C,
	// Added values after version 2.5
	0x103,0x103,
	// Canon MakerNote Fields
	0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,0x927C,0x927C,
	0x927C,0x927C,0x927C,
	
	0x8825,0x8825,0x8825,0x8825,0x8825,0x8825
	};


enum MultipleChoiceIndexes {
 ExposureProgramIndex=14,
 FlashIndex=17,
 LightSourceIndex=21,
 MeteringModeIndex=25,
 OrientationIndex=27,
 ResolutionUnitIndex=32,
 YCbCrIndex=36,
 SensingMethodIndex=37,
 ExposureModeIndex=38,
 WhiteBalanceIndex=39,
 SceneCaptureIndex=42,
 GainControlIndex=43,
 ContrastIndex=44,
 SaturationIndex=45,
 SharpnessIndex=46,
 SubjectDistanceIndex=47,
 CompressionIndex=49,

 CanonMacroModeIndex=50,
 CanonFlashModeIndex=51,
 CanonContinousIndex=52,
 CanonFocusModeIndex=53,
 CanonImageSizeIndex=54,
 CanonEasyShootingIndex=55,
 CanonDigitalZoomIndex=56,
 CanonContrastIndex=57,
 CanonSaturationIndex=58,
 CanonSharpnessIndex=59,
 CanonISOSpeedIndex=60,
 CanonMeteringModeIndex=61,
 CanonFocusTypeIndex=62,
 CanonAFSelectIndex=63,
 CanonExposureModeIndex=64,
 CanonFlashActivityIndex=65,
 CanonWhiteBalanceIndex=66,
 CanonFlashBiasIndex=67,
 GpsLatitudeIndex=73,
 GpsLongitudeIndex=74,
 GpsAltitudeIndex=75,
};


char* ExposureProgramUnits[9]={"unknown","manual control", "normal program",
 "aperture priority", "shutter priority", "creative (slow program)",
 "action (high-speed)", "portrait mode", "landscape mode"};
char* ExposureProgramUnitStr="unknown|manual control|normal program|aperture priority|shutter priority|creative (slow program)|action (high-speed)|portrait mode|landscape mode";

// Values for LightSource were not correct for version 1.4 - corrected

char* LightSourceUnits[21]={"unknown", "daylight", "fluorescent", "tungsten", "flash",
		"fine weather", "cloudy weather", "shade", "daylight fluorescent", "day white fluorescent", "cool white fluorescent", "white fluorescent",
		"standard light A", "standard light B", "standard light C",
		"D55", "D65", "D75", "D50", "ISO studio tungsten", "other"};
char* LightSourceUnitStr="unknown|daylight|fluorescent|tungsten|flash|fine weather|cloudy weather|shade|daylight fluorescent|day white fluorescent|cool white fluorescent|white fluorescent|standard light A|standard light B|standard light C|D55|D65|D75|D50|ISO studio tungsten|other";

char* OrientationUnits[9]={"unknown","Top left","Top right",
		"Bottom right","Bottom left",
		"Left top","Right top",
		"Right bottom","Left bottom"};
char* OrientationUnitStr="unknown|Top left|Top right|Bottom right|Bottom left|Left top|Right top|Right bottom|Left bottom";

char* FlashUnits[8]={"not fired","fired","forced on",
		"forced off","not fired, auto",
		"fired, auto","forced on, red eye",
		"fired, auto, red eye"};
char* FlashUnitsStr="not fired|fired|forced on|forced off|not fired, auto|fired, auto|forced on, red eye|fired, auto, red eye";

char* MeteringModeUnits[8]={"unknown", "average", "center weighted average",
"spot", "multi-spot", "pattern", "partial", "other"};
char* MeteringModeUnitsStr="unknown|average|center weighted average|spot|multi-spot|pattern|partial|other";

char* ResolutionUnits[2]={"inch","centimeter"};
char* ResolutionUnitsStr="inch|centimeter";

char* YCbCrUnits[2]={"centered","co-sited"};
char* YCbCrUnitsStr = "centered|co-sited";

char* SensingMethodUnits[7]={"not defined","one-chip color area sensor","two-chip color area sensor","three-chip color area sensor","color sequential area sensor","trilinear sensor","color sequential linear sensor"};
char* SensingMethodUnitsStr="not defined|one-chip color area sensor|two-chip color area sensor|three-chip color area sensor|color sequential area sensor|trilinear sensor|color sequential linear sensor";

char* ExposureModeUnits[3]={"auto exposure","manual exposure","auto bracket"};
char* ExposureModeUnitsStr="auto exposure|manual exposure|auto bracket";

char* WhiteBalanceUnits[2]={"auto white balance","manual white balance"};
char* WhiteBalanceUnitsStr="auto white balance|manual white balance";

char* SceneCaptureUnits[4]={"standard","landscape","portrait","night scene"};
char* SceneCaptureUnitsStr="standard|landscape|portrait|night scene";

char* GainControlUnits[5]={"none","low gain up","high gain up","low gain down","high gain down"};
char* GainControlUnitsStr="none|low gain up|high gain up|low gain down|high gain down";

char* ContrastUnits[3]={"normal","soft","hard"};
char* ContrastUnitsStr="normal|soft|hard";

char* SaturationUnits[3]={"normal","low saturation","high saturation"};
char* SaturationUnitsStr="normal|low saturation|high saturation";

char* SharpnessUnits[3]={"normal","soft","hard"};
char* SharpnessUnitsStr="normal|soft|hard";

char* SubjectDistanceUnits[4]={"unknown","macro","close view","distant view"};
char* SubjectDistanceUnitsStr="unknown|macro|close view|distant view";

char* CanonMacroModeUnits[2]={"macro","normal"};
char* CanonMacroModeUnitsStr="macro|normal";

char* CanonFlashModeUnits[8]={"not fired","auto","on","red-eye reduction","slow synchro","auto + red-eye reduction","on + red-eye reduction","external flash"};
char* CanonFlashModeUnitsStr="not fired|auto|on|red-eye reduction|slow synchro|auto + red-eye reduction|on + red-eye reduction|external flash";

char* CanonContinousUnits[2]={"single","continuous"};
char* CanonContinousUnitsStr="single|continuous";

char* CanonFocusModeUnits[7]={"One-Shot","AI Servo","AI Focus","Manual Focus","Single","Continuous","Manual Focus"};
char* CanonFocusModeUnitsStr="One-Shot|AI Servo|AI Focus|Manual Focus|Single|Continuous|Manual Focus";

char* CanonImageSizeUnits[3]={"Large","Medium","Small"};
char* CanonImageSizeUnitsStr="Large|Medium|Small";

char* CanonEasyShootingUnits[12]={"Full Auto","Manual","Landscape","Fast Shutter","Slow Shutter","Night","B&W","Sepia","Portrait","Sports","Macro/Close-Up","Pan Focus"};
char* CanonEasyShootingUnitsStr="Full Auto|Manual|Landscape|Fast Shutter|Slow Shutter|Night|B&W|Sepia|Portrait|Sports|Macro/Close-Up|Pan Focus";

char* CanonDigitalZoomUnits[3]={"No digital zoom","2X","4X"};
char* CanonDigitalZoomUnitsStr="No digital zoom|2X|4X";

char* CanonCSSUnits[3]={"Normal","High","Low"};
char* CanonCSSUnitsStr="Normal|High|Low";

char* CanonISOSpeedUnits[6]={"Unknown","Auto ISO","ISO 50","ISO 100","ISO 200","ISO 400"};
char* CanonISOSpeedUnitsStr="Unknown|Auto ISO|ISO 50|ISO 100|ISO 200|ISO 400";

char* CanonMeteringModeUnits[3]={"Evaluative","Partial","Centre Weighted"};
char* CanonMeteringModeUnitsStr="Evaluative|Partial|Centre Weighted";

char* CanonFocusTypeUnits[4]={"Manual","Auto","Close-up","Locked"};
char* CanonFocusTypeUnitsStr="Manual|Auto|Close-up|Locked";

char* CanonAFSelectUnits[5]={"None","Auto Selected","Right","Centre","Left"};
char* CanonAFSelectUnitsStr="None|Auto Selected|Right|Centre|Left";

char* CanonExposureModeUnits[6]={"Easy shooting","Program","Tv-priority","Av-priority","Manual","A-DEP"};
char* CanonExposureModeUnitsStr="Easy shooting|Program|Tv-priority|Av-priority|Manual|A-DEP";

char* CanonFlashActivityUnits[2]={"Flash did not fire","Flash fired"};
char* CanonFlashActivityUnitsStr="Flash did not fire|Flash fired";

char* CanonWhiteBalanceUnits[7]={"Auto","Sunny","Cloudy","Tungsten","Fluorescent","Flash","Custom"};
char* CanonWhiteBalanceUnitsStr="Auto|Sunny|Cloudy|Tungsten|Fluorescent|Flash|Custom";

char* CanonFlashBiasUnits[17]={"-2 EV","-1.67 EV","-1.5 EV","-1.33 EV","-1 EV","-0.67 EV","-0.5 EV","-0.33 EV","0 EV","0.33 EV","0.5 EV","0.67 EV","1 EV","1.33 EV","1.5 EV","1.67 EV","2 EV"};
char* CanonFlashBiasUnitsStr="-2 EV|-1.67 EV|-1.5 EV|-1.33 EV|-1 EV|-0.67 EV|-0.5 EV|-0.33 EV|0 EV|0.33 EV|0.5 EV|0.67 EV|1 EV|1.33 EV|1.5 EV|1.67 EV|2 EV";

char* GpsUnitsStr="String|Floating|h|m|s|direction";
char* GpsHeightUnitsStr="m|ft";

int CompressionNr[49]={1,2,3,4,5,6,7,8,9,10,99,262,32766,32767,32769,32770,32771,32772,32773,32809,32867,32895,32896,32897,32898,32908,32909,32946,32947,33003,33005,34661,34676,34677};
char* CompressionName[49+1]={"Uncompressed","CCITT 1D","T4/Group 3 Fax","T6/Group 4 Fax","LZW","JPEG (old-style)","JPEG","Adobe Deflate","JBIG B&W","JBIG Color","JPEG","Kodak 262","Next",
  "Sony ARW Compressed","Packed RAW","Samsung SRW Compressed","CCIRLEW","Samsung SRW Compressed 2","PackBits","Thunderscan","Kodak KDC Compressed","IT8CTPAD","IT8LW","IT8MP","IT8BL",
  "PixarFilm","PixarLog","Deflate","DCS","Aperio JPEG 2000 YCbCr","Aperio JPEG 2000 RGB","JBIG","SGILog","SGILog24","JPEG 2000","Nikon NEF Compressed","JBIG2 TIFF FX",
  "Microsoft Document Imaging (MDI) Binary Level Codec","Microsoft Document Imaging (MDI) Progressive Transform Codec","Microsoft Document Imaging (MDI) Vector","ESRI Lerc",
  "Lossy JPEG","LZMA2","Zstd","WebP","PNG","JPEG XR","Kodak DCR Compressed","Pentax PEF Compressed","UNKNOWN"};
char* CompressionNameStr={"Uncompressed|CCITT 1D|T4/Group 3 Fax|T6/Group 4 Fax|LZW|JPEG (old-style)|JPEG|Adobe Deflate|JBIG B&W|JBIG Color|JPEG|Kodak 262|Next" \
  "Sony ARW Compressed|Packed RAW|Samsung SRW Compressed|CCIRLEW|Samsung SRW Compressed 2|PackBits|Thunderscan|Kodak KDC Compressed|IT8CTPAD|IT8LW|IT8MP|IT8BL" \
  "PixarFilm|PixarLog|Deflate|DCS|Aperio JPEG 2000 YCbCr|Aperio JPEG 2000 RGB|JBIG|SGILog|SGILog24|JPEG 2000|Nikon NEF Compressed|JBIG2 TIFF FX" \
  "Microsoft Document Imaging (MDI) Binary Level Codec|Microsoft Document Imaging (MDI) Progressive Transform Codec|Microsoft Document Imaging (MDI) Vector|ESRI Lerc" \
  "Lossy JPEG|LZMA2|Zstd|WebP|PNG|JPEG XR|Kodak DCR Compressed|Pentax PEF Compressed|UNKNOWN"};

char* strlcpy(char* dst,const char* src,int maxlen)     // This function assumes there is room for maxlen characters
{														//  + one space for the null character
	if ((int)strlen(src)>=maxlen) {
		strncpy(dst,src,maxlen);
		dst[maxlen]=0;
	} else
		strcpy(dst,src);
	return dst;
}

#define INT_DIGITS 19		/* enough for 64 bit integer */

char *itoa(i)
     int i;
{
  /* Room for INT_DIGITS digits, - and '\0' */
  static char buf[INT_DIGITS + 2];
  char *p = buf + INT_DIGITS + 1;	/* points to terminating '\0' */
  if (i >= 0) {
    do {
      *--p = '0' + (i % 10);
      i /= 10;
    } while (i != 0);
    return p;
  }
  else {			/* i < 0 */
    do {
      *--p = '0' - (i % 10);
      i /= 10;
    } while (i != 0);
    *--p = '-';
  }
  return p;
}

int DCPCALL ContentGetDetectString(char* DetectString,int maxlen)
{
	strlcpy(DetectString,_detectstring,maxlen);
	return 0;
}

int DCPCALL ContentGetSupportedField(int FieldIndex,char* FieldName,char* Units,int maxlen)
{

	char* UnitsStrPointer;

	if (FieldIndex<0 || FieldIndex>=fieldcount)
		return ft_nomorefields;
	strlcpy(FieldName,fieldnames[FieldIndex],maxlen-1);
	Units[0]=0;

	switch (FieldIndex){

		case(ExposureProgramIndex): UnitsStrPointer= ExposureProgramUnitStr; break;
		case(LightSourceIndex):     UnitsStrPointer=LightSourceUnitStr; break;
		case(OrientationIndex):		UnitsStrPointer=OrientationUnitStr; break;
		case(FlashIndex):			UnitsStrPointer=FlashUnitsStr; break;
		case(MeteringModeIndex):    UnitsStrPointer=MeteringModeUnitsStr; break;
		case(ResolutionUnitIndex):  UnitsStrPointer=ResolutionUnitsStr; break;
		case(YCbCrIndex):		    UnitsStrPointer=YCbCrUnitsStr; break;
		case(SensingMethodIndex):   UnitsStrPointer=SensingMethodUnitsStr; break;
		case(ExposureModeIndex):    UnitsStrPointer=ExposureModeUnitsStr; break;
		case(WhiteBalanceIndex):    UnitsStrPointer=WhiteBalanceUnitsStr; break;
		case(SceneCaptureIndex):    UnitsStrPointer=SceneCaptureUnitsStr; break;
		case(GainControlIndex):     UnitsStrPointer=GainControlUnitsStr; break;
		case(ContrastIndex):        UnitsStrPointer=ContrastUnitsStr; break;
		case(SaturationIndex):      UnitsStrPointer=SaturationUnitsStr; break;
		case(SharpnessIndex):       UnitsStrPointer=SharpnessUnitsStr; break;
		case(SubjectDistanceIndex): UnitsStrPointer=SubjectDistanceUnitsStr; break;
		case(CompressionIndex):     UnitsStrPointer=CompressionNameStr; break;
		case(CanonMacroModeIndex):  UnitsStrPointer=CanonMacroModeUnitsStr; break;
		case(CanonFlashModeIndex):  UnitsStrPointer=CanonFlashModeUnitsStr; break;
		case(CanonContinousIndex):  UnitsStrPointer=CanonContinousUnitsStr; break;
		case(CanonFocusModeIndex):  UnitsStrPointer=CanonFocusModeUnitsStr; break;
		case(CanonImageSizeIndex):  UnitsStrPointer=CanonImageSizeUnitsStr; break;
		case(CanonEasyShootingIndex):  UnitsStrPointer=CanonEasyShootingUnitsStr; break;
		case(CanonDigitalZoomIndex):  UnitsStrPointer=CanonDigitalZoomUnitsStr; break;
		case(CanonContrastIndex):
		case(CanonSaturationIndex):
		case(CanonSharpnessIndex):  UnitsStrPointer=CanonCSSUnitsStr; break;
		case(CanonISOSpeedIndex):   UnitsStrPointer=CanonMeteringModeUnitsStr; break;
		case(CanonMeteringModeIndex): UnitsStrPointer=CanonMeteringModeUnitsStr; break;
		case(CanonFocusTypeIndex): UnitsStrPointer=CanonFocusTypeUnitsStr; break;
		case(CanonAFSelectIndex): UnitsStrPointer=CanonAFSelectUnitsStr; break;
		case(CanonExposureModeIndex): UnitsStrPointer=CanonExposureModeUnitsStr; break;
		case(CanonFlashActivityIndex): UnitsStrPointer=CanonFlashActivityUnitsStr; break;
		case(CanonWhiteBalanceIndex): UnitsStrPointer=CanonWhiteBalanceUnitsStr; break;
		case(CanonFlashBiasIndex): UnitsStrPointer=CanonFlashBiasUnitsStr; break;
		case(GpsLatitudeIndex):
		case(GpsLongitudeIndex):   UnitsStrPointer=GpsUnitsStr; break;
		case(GpsAltitudeIndex):		UnitsStrPointer=GpsHeightUnitsStr; break;
		default:				    UnitsStrPointer=NULL;
	}


	if(UnitsStrPointer != NULL)
        strlcpy(Units,UnitsStrPointer,maxlen-1);

	return fieldtypes[FieldIndex];
}

WORD get2bytes(char* p,BOOL MotorolaEndian)
{
	byte* p2=(byte*)p;
	if (MotorolaEndian) {
		return (WORD)p2[1]+(((WORD)p2[0])<<8);
	} else {
		return *(WORD*)p;
	}
}

DWORD get4bytes(char* p,BOOL MotorolaEndian)
{
	byte* p2=(byte*)p;
	if (MotorolaEndian) {
		return (DWORD)p2[3]+(((DWORD)p2[2])<<8)+(((DWORD)p2[1])<<16)+(((DWORD)p2[0])<<24);
	} else {
		return *(DWORD*)p2;
	}
}

#define tagtype_byte 1
#define tagtype_pchar 2
#define tagtype_ushort 3
#define tagtype_ulong 4
#define tagtype_urational 5
#define tagtype_sbyte 6             // Changed after 1.4 since this is more accurate
#define tagtype_undef 7
#define tagtype_short 8
#define tagtype_long 9
#define tagtype_rational 10
#define tagtype_float 11			// Changed after 1.4 since this is more accurate
#define tagtype_double 12


typedef union {
	int i[2];
	double d;
} MIXTYPE;

const int bytesizetab[13]={0,1,1,2,4,8,1,1,2,4,8,4,8};

int bytespertype(int tagtype)
{
	if (tagtype>=tagtype_byte && tagtype<=tagtype_double)
		return bytesizetab[tagtype];
	else
		return 0;
}

void replacechars(char* p,char inchar,char outchar)
{
	while (p[0]) {
		if (p[0]==inchar)
			p[0]=outchar;
		p++;
	}
}

int GetTagDataOffset(char* data,int offset,int TagNeeded,BOOL MotorolaEndian,int datalen){

	int i;
	int tagtype=0;
	int tagdataoffset=-1;
	int tagnumelements=0;

	int entrycount=get2bytes(data+offset,MotorolaEndian);
	if (entrycount==0 || 
		offset+2+entrycount*12+4>datalen)
		return -1;
	
	for (i=0;i<entrycount;i++) {
		int ifd0tag=get2bytes(data+offset+2+12*i,MotorolaEndian);
		if (ifd0tag==TagNeeded) {
			tagtype=get2bytes(data+offset+2+12*i+2,MotorolaEndian);
			tagnumelements=(int)get4bytes(data+offset+2+12*i+4,MotorolaEndian);
			if (tagnumelements*bytespertype(tagtype)>4)
				tagdataoffset=(int)get4bytes(data+offset+2+12*i+8,MotorolaEndian);
			else
				tagdataoffset=offset+2+12*i+8;
			break;
		}
	}
	return tagdataoffset;
}

#define GPSLatitudeRef 1
#define GPSLatitude 2
#define GPSLongitudeRef 3
#define GPSLongitude 4
#define GPSAltitudeRef 5
#define GPSAltitude 6
#define GPSTimeStamp 7
#define GPSImageDirection 0x11
#define GPSDateStamp 0x1d

double ParseGps(char* data,int datalen,int makernoteoffset,int FieldIndex,BOOL MotorolaEndian,char* bufval,int UnitIndex)
{
	int tagdataoffset;
	char signbuf[2];
	double result=0;
	bufval[0]=0;   // Alternate text
	if (FieldIndex==gps_start || FieldIndex==gps_start+1) {  //Latitude or Longitude
		tagdataoffset=GetTagDataOffset(data,makernoteoffset,
			FieldIndex==gps_start ? GPSLatitudeRef:GPSLongitudeRef,MotorolaEndian,datalen);
		if(tagdataoffset==-1)				// The tag was not found in the header
			return 1e100;
		strncpy(signbuf,data+tagdataoffset,2);

		tagdataoffset=GetTagDataOffset(data,makernoteoffset,
			FieldIndex==gps_start ? GPSLatitude:GPSLongitude,MotorolaEndian,datalen);
		if(tagdataoffset==-1)				// The tag was not found in the header
			return 1e100;
		// GPS data is stored as 3 urational values: degrees, arc minutes, arc seconds! Often arc seconds are saved as a fraction of the minutes!
		double prevvalue=0;
		for (int i=0;i<3;i++) {
			int numvalue=(int)get4bytes(data+tagdataoffset+8*i,MotorolaEndian);
			int numvalue2=(int)get4bytes(data+tagdataoffset+8*i+4,MotorolaEndian);
			double doublevalue=0;
			if (numvalue2) {
				doublevalue=(DWORD)numvalue;
				doublevalue/=(DWORD)numvalue2;
			} else
				doublevalue=0;
			if (i==2 && doublevalue==0 && prevvalue!=0) {
				// Arc seconds stored within arc minutes!
				doublevalue=(prevvalue*60)-((int)prevvalue)*60;
			}
			prevvalue=doublevalue;
			if (i==1)
				doublevalue=(int)doublevalue;   // cut off fractional seconds!
			switch (UnitIndex) {
			case 0:  //string
			case 1:  //numeric
				if (i>0 && (int)doublevalue<10)
					strcat(bufval,"0");
				itoa((int)doublevalue,bufval+strlen(bufval),10);
				if (i==0) {
					strcat(bufval,"°");
					result=doublevalue;
				} else if (i==1) {
					strcat(bufval,"'");
					result+=doublevalue/60;
				} else {
					int fractional=(int)(doublevalue*1000)-((int)doublevalue)*1000;
					if (fractional>=100) {
						strcat(bufval,".");
						itoa(fractional,bufval+strlen(bufval),10);
					} else if (fractional>=10) {
						strcat(bufval,".0");
						itoa(fractional,bufval+strlen(bufval),10);
					} else if (fractional>0) {
						strcat(bufval,".00");
						itoa(fractional,bufval+strlen(bufval),10);
					}
					strcat(bufval,"\"");
					result+=doublevalue/3600;
				}
				break;
			case 2:  // h
				if (i==0) {
					itoa((int)doublevalue,bufval+strlen(bufval),10);
					result=doublevalue;
				}
				break;
			case 3:  // m
				if (i==1) {
					if ((int)doublevalue<10)
						strcpy(bufval,"0");
					itoa((int)doublevalue,bufval+strlen(bufval),10);
					result=doublevalue;
				}
				break;
			case 4:  // s
				if (i==2) {
					if ((int)doublevalue<10)
						strcpy(bufval,"0");
					itoa((int)doublevalue,bufval+strlen(bufval),10);
					result=doublevalue;
				}
				break;
			case 5:  // direction
				signbuf[1]=0;
				strcpy(bufval,signbuf);
				break;
			}
		}
		if (UnitIndex<=1 && (signbuf[0]=='S' || signbuf[0]=='W'))
			result=-result;
		signbuf[1]=0;
		if (UnitIndex==0)
			strcat(bufval,signbuf);
		else if (UnitIndex==1) {
			sprintf(bufval,"%1.6f",result);
		}
		return result;
	} else 	if (FieldIndex==gps_start+2) {  //Altitude
		tagdataoffset=GetTagDataOffset(data,makernoteoffset,GPSAltitudeRef,MotorolaEndian,datalen);
		if(tagdataoffset==-1)				// The tag was not found in the header
			return 1e100;
		signbuf[0]=(char)data[tagdataoffset];

		tagdataoffset=GetTagDataOffset(data,makernoteoffset,GPSAltitude,MotorolaEndian,datalen);
		if(tagdataoffset==-1)				// The tag was not found in the header
			return 1e100;
		// GPS data is stored as 3 urational values: degrees, arc minutes, arc seconds!
		int numvalue=(int)get4bytes(data+tagdataoffset,MotorolaEndian);
		int numvalue2=(int)get4bytes(data+tagdataoffset+4,MotorolaEndian);
		if (numvalue2) {
			double doublevalue=(DWORD)numvalue;
			doublevalue/=(DWORD)numvalue2;
			if (signbuf[0]==1)
				strcpy(bufval,"-");
			if (UnitIndex==1)
				doublevalue*=3.2808399;  //feet!
			itoa((int)doublevalue,bufval+strlen(bufval),10);
			int fractional=(int)(doublevalue*100)-((int)doublevalue)*100;
			if (fractional>=10) {
				strcat(bufval,".");
				itoa(fractional,bufval+strlen(bufval),10);
			} else if (fractional>0) {
				strcat(bufval,".0");
				itoa(fractional,bufval+strlen(bufval),10);
			}
			if (UnitIndex==1)
				strcat(bufval,"ft");
			else
				strcat(bufval,"m");
			result=doublevalue;
		}
		if (signbuf[0]==1)
			result=-result;
		return result;
	} else 	if (FieldIndex==gps_start+3) {  //Timestamp
		tagdataoffset=GetTagDataOffset(data,makernoteoffset,
			GPSTimeStamp,MotorolaEndian,datalen);
		if(tagdataoffset==-1)				// The tag was not found in the header
			return 1e100;
		// GPS data is stored as 3 urational values: hours, minutes, seconds
		// double prevvalue=0;
		strcpy(bufval,"           ");  //empty date stamp
		for (int i=0;i<3;i++) {
			int numvalue=(int)get4bytes(data+tagdataoffset+8*i,MotorolaEndian);
			int numvalue2=(int)get4bytes(data+tagdataoffset+8*i+4,MotorolaEndian);
			double doublevalue=0;
			if (numvalue2) {
				doublevalue=(DWORD)numvalue;
				doublevalue/=(DWORD)numvalue2;
			} else
				doublevalue=0;
			itoa((int)doublevalue,bufval+strlen(bufval),10);
			if (i<2)
				strcat(bufval," ");
		}
		return 0;
	} else 	if (FieldIndex==gps_start+4) {  //Image direction (degrees)
		tagdataoffset=GetTagDataOffset(data,makernoteoffset,GPSImageDirection,MotorolaEndian,datalen);
		if(tagdataoffset==-1)				// The tag was not found in the header
			return 1e100;
		// GPS direction is stored as 1 urational value
		int numvalue=(int)get4bytes(data+tagdataoffset,MotorolaEndian);
		int numvalue2=(int)get4bytes(data+tagdataoffset+4,MotorolaEndian);
		if (numvalue2) {
			double doublevalue=(DWORD)numvalue;
			doublevalue/=(DWORD)numvalue2;
			itoa((int)doublevalue,bufval+strlen(bufval),10);
			int fractional=(int)(doublevalue*100)-((int)doublevalue)*100;
			if (fractional>=10) {
				strcat(bufval,".");
				itoa(fractional,bufval+strlen(bufval),10);
			} else if (fractional>0) {
				strcat(bufval,".0");
				itoa(fractional,bufval+strlen(bufval),10);
			}
			strcat(bufval,"°");
			result=doublevalue;
		}
		return result;
	} else 	if (FieldIndex==gps_start+5) {  //Datestamp
		tagdataoffset=GetTagDataOffset(data,makernoteoffset,
			GPSDateStamp,MotorolaEndian,datalen);
		if(tagdataoffset==-1)				// The tag was not found in the header
			return 1e100;
		// GPS date stamp is stored as 11 character string
		char* pcharvalue=data+tagdataoffset;
		if (pcharvalue[0]>='0' && pcharvalue[0]<='9')
			strlcpy(bufval,pcharvalue,11);
		return 0;
	}
	return 1e100;
}

int CanonMakerNote(char* data,int datalen,int makernoteoffset,int FieldIndex,BOOL MotorolaEndian,char* bufval){

	int tagnum = 1;
	int tagdataoffset;
	char make[32];


	if (FieldIndex<canon_start || FieldIndex>canon_end)
		return -1;

	if(ParseTiffFile(INVALID_HANDLE_VALUE,data,datalen,0x010F,0x010F,ft_string,22,make,31,false,0)==false)
		return -1;
    
	if(strcasecmp(make,"Canon")!=0)
		return -1;
	
	FieldIndex-=canon_start;

	if (FieldIndex>=0 && FieldIndex<=15)
		tagnum = 1;
	else if(FieldIndex==16 || FieldIndex==17)
		tagnum = 4;
	else if(FieldIndex==18)
		tagnum = 6;
	else if(FieldIndex==19)
		tagnum = 7;
	else if(FieldIndex==20)
		tagnum = 8;
	else if(FieldIndex==21)
		tagnum = 9;
	else if(FieldIndex==22)
		tagnum = 12;


	tagdataoffset=GetTagDataOffset(data,makernoteoffset,tagnum,MotorolaEndian,datalen);

	if(tagdataoffset==-1)				// The tag was not found in the header
		return -1;

	if(tagnum==1 || tagnum==4){			// These are muktiple choice fields

		int offset=0;
		
		switch(FieldIndex){
			case 0:       //Macro Mode
				offset=1;
				break;		
			case 1:      // Flash Mode
				offset=4;
				break;
			case 2:      // Continous Mode
				offset=5;
				break;
			case 3:		// Focus Mode
				offset=7;
				break;
			case 4:		// Image size
			case 5:		// Easy Shooting
			case 6:		// Digital Zoom
			case 7:		// Contrast
			case 8:		// Saturation
			case 9:		// Sharpness
			case 10:	// ISO Speed
			case 11:	// Metering Mode
			case 12:	// Focus Type
			case 13:	// AF point select
			case 14:	// Exposure mode
				offset=FieldIndex+6;
				break;
			case 15:	// Flash Activity
				offset=28;
				break;
			case 16:	// White Balance
				offset=7;
				break;
			case 17:	// Flash bias
				offset=15;
				break;

		}

		bufval[0]=0;
		return (unsigned short)get2bytes(data+tagdataoffset+2*offset,MotorolaEndian);
	}
	else if(tagnum==6 || tagnum==7 || tagnum==9){			// ASCII string
		strlcpy(bufval,data+tagdataoffset,32);
        return 0;
	}
	else if (tagnum==8 || tagnum==12){						// numeric value
		bufval[0]=0;
		return (unsigned long)get4bytes(data+tagdataoffset,MotorolaEndian);
	}

	return -1;

}


BOOL ParseTiffFile(int f,char* data,int datalen,int TagNeeded,int TagNeeded2,int FormatNeeded,int FieldIndex, void* FieldValue,int maxlen,BOOL unicode,int UnitIndex)
{
	int i;
	BOOL MotorolaEndian=(data[0]=='M');
	BOOL found;

	// First, go to IFD0 offset, which contains info about main image
	int ifd0offset=(int)get4bytes(data+4,MotorolaEndian);
	if (ifd0offset+6>datalen)
		return false;
	int ifd0entrycount=get2bytes(data+ifd0offset,MotorolaEndian);
	if (ifd0entrycount==0 || 
		ifd0offset+2+ifd0entrycount*12+4>datalen)
		return false;
	// first look for tag at top level
	// or find tag 0x8769, which contains the EXIF data
	
	int tagdataoffset=0;
	int tagnumelements=0;
	int tagtype=0;
	int tagfound=0;
	int exifoffset=0;
	for (i=0;i<ifd0entrycount;i++) {
		int ifd0tag=get2bytes(data+ifd0offset+2+12*i,MotorolaEndian);
		if (ifd0tag==TagNeeded || ifd0tag==TagNeeded2) {
			tagfound=ifd0tag;
			tagtype=get2bytes(data+ifd0offset+2+12*i+2,MotorolaEndian);
			tagnumelements=(int)get4bytes(data+ifd0offset+2+12*i+4,MotorolaEndian);
			if (tagnumelements*bytespertype(tagtype)>4)
				tagdataoffset=(int)get4bytes(data+ifd0offset+2+12*i+8,MotorolaEndian);
			else
				tagdataoffset=ifd0offset+2+12*i+8;
			break;
		} else if (ifd0tag==0x8769) {													// Check if we have found a tag indicating the existance of a Exif SubIFD	
			// int ifd0type=get2bytes(data+ifd0offset+2+12*i+2,MotorolaEndian);
			exifoffset=(int)get4bytes(data+ifd0offset+2+12*i+8,MotorolaEndian);
		}
	}
	if (tagtype==0 && exifoffset==0)
		return false;
	int exifoffsetfromstart=0;
	// now we have the offset of the EXIF data -> parse them!
	if (tagtype==0) {
		if (exifoffset+2>datalen) {  // special case of exif data outside the data block!
			if (f==INVALID_HANDLE_VALUE)
				return false;
			int newdataoffset=lseek(f,0,SEEK_CUR)-datalen+exifoffset;
			if (-1==lseek(f,newdataoffset,SEEK_SET))
				return false;
			datalen=64*1024;  // max buffer size, see below
			datalen = read(f,data,64*1024);
			exifoffsetfromstart=exifoffset;
			exifoffset=0;
		}
		int exifentrycount=get2bytes(data+exifoffset,MotorolaEndian);
		if (exifentrycount==0 || 
			exifoffset+2+exifentrycount*12+4>datalen)
			return false;
		// now find the requested tag!
		for (i=0;i<exifentrycount;i++) {
			int ifd0tag=get2bytes(data+exifoffset+2+12*i,MotorolaEndian);
			if (ifd0tag==TagNeeded || ifd0tag==TagNeeded2) {
				tagfound=ifd0tag;
				tagtype=get2bytes(data+exifoffset+2+12*i+2,MotorolaEndian);
				tagnumelements=(int)get4bytes(data+exifoffset+2+12*i+4,MotorolaEndian);
				if (tagnumelements*bytespertype(tagtype)>4)
					tagdataoffset=(int)get4bytes(data+exifoffset+2+12*i+8,MotorolaEndian)-exifoffsetfromstart;
				else
					tagdataoffset=exifoffset+2+12*i+8;
				break;
			}
		}
		if (tagtype==0 || tagdataoffset<0 || tagdataoffset>datalen)
			return false;  // tag not present, or offset out of buffer
	}	
	// now convert the data from tagtype to FormatNeeded
	int numvalue=0;
	int numvalue2=0;
	int Year=0,Month=0,Day=0,Hour=0,Min=0,Sec=0;
	int l,j;
	int nr;
	char chr1,chr2;
	double doublevalue=0;
	char bufval[1024];
	bufval[0]=0;
	char* pcharvalue=bufval;
	MIXTYPE mix;

	if (TagNeeded==0x8825) {
		if (tagtype==tagtype_ulong)
			tagdataoffset=get4bytes(data+tagdataoffset,MotorolaEndian);
		tagtype=tagtype_undef;   //GPS!
		tagnumelements=0;
	}
	
	switch (tagtype) {
	case tagtype_byte:
		numvalue=(byte)data[tagdataoffset];
		doublevalue=numvalue;
		break;
	case tagtype_pchar:
		pcharvalue=data+tagdataoffset;
		numvalue=0;
		doublevalue=0;
		break;
	case tagtype_ushort:
		numvalue=(unsigned short)get2bytes(data+tagdataoffset,MotorolaEndian);
		doublevalue=numvalue;
		break;
	case tagtype_ulong:
		numvalue=get4bytes(data+tagdataoffset,MotorolaEndian);
		doublevalue=numvalue;
		break;
	case tagtype_urational:
		numvalue=(int)get4bytes(data+tagdataoffset,MotorolaEndian);
		numvalue2=(int)get4bytes(data+tagdataoffset+4,MotorolaEndian);
		if (numvalue2) {
			doublevalue=(DWORD)numvalue;
			doublevalue/=(DWORD)numvalue2;
		} else
			doublevalue=0;
		break;
	case tagtype_sbyte:							// Changed after 1.4 since this is more accurate
		numvalue=(char)data[tagdataoffset];
		doublevalue=numvalue;
		break;
	case tagtype_undef:
		if (tagnumelements==1) {
			numvalue=data[tagdataoffset];
			doublevalue=numvalue;
		} else {
			tagtype=tagtype_pchar;
			switch(tagfound) {
			case 0x9000:  //exif version
				strlcpy(bufval,data+tagdataoffset,4);
				break;
			case 0x9101:  //ComponentsConfiguration
				if (data[tagdataoffset]=='4')
					strcpy(bufval,"RGB");
				else
					strcpy(bufval,"YCbCr");
				break;
			case 0x9286:  //UserComment
				if (tagnumelements-6 < maxlen)		// This lines are just to ensure there is no buffer overflow
					nr = tagnumelements-8;			// tagnumelements = the number of bytes in the comment including the 8 for the specifier
				else
					nr = maxlen - 1;
				if(nr>=sizeof(bufval))				// This is a limit for our buffer
					nr = sizeof(bufval)-1;

				if (strncmp(data+tagdataoffset,"ASCII",8)==0){			// If this is a simple ascii string
					strncpy(bufval,data+tagdataoffset+8,nr);
					bufval[nr]=0;
				}
				else if (strncmp(data+tagdataoffset,"UNICODE",8)==0){   // If this is a unicode string convert it to ascii
					chr1 = data[nr];
					chr2 = data[nr+1];
					data[tagdataoffset+tagnumelements]=0;				// Since the string is not neccerilly NULL terminated - make it so
					data[tagdataoffset+tagnumelements+1]=0;
					/*
					if(!WideCharToMultiByte(CP_ACP,0,(LPCWSTR)(data+tagdataoffset+8),nr/2+1,bufval,nr,NULL,NULL))
						bufval[0]=0;
					*/
					bufval[nr]=0;
					data[nr]=chr1;				// Just to be on the safe side
					data[nr+1]=chr2;
				}
				break;
			case 0x927C:  //MakerNote
				nr = -1;
				if (FieldIndex>=canon_start && FieldIndex<=canon_end)
					nr =CanonMakerNote(data,datalen,tagdataoffset,FieldIndex,MotorolaEndian,bufval);

				if(nr==-1)
					return false;
				else
				  numvalue = nr;

				if(FieldIndex==68)  // Canon Image number needs special handling
					sprintf(bufval,"%d-%03d",numvalue/1000,numvalue%1000);
				else if (FieldIndex==70)   // Canon camrea serial number needs special care
					sprintf(bufval, "%04X-%05X",numvalue&0xFFFF,numvalue>>16);

				break;

			case 0x8825:  //GPS
				doublevalue = 1e100;  //invalid degrees
				if (FieldIndex>=gps_start && FieldIndex<=gps_end)
					doublevalue =ParseGps(data,datalen,tagdataoffset,FieldIndex,MotorolaEndian,bufval,UnitIndex);

				if(doublevalue>=1e99)
					return false;
				break;
			default:
				l=min(sizeof(bufval)-2,tagnumelements);
				strlcpy(bufval,data+tagdataoffset,l);
				bufval[l+1]=0;
			}
		}
		break;
	case tagtype_short:
		numvalue=(short)get2bytes(data+tagdataoffset,MotorolaEndian);
		doublevalue=numvalue;
		break;
	case tagtype_long:
		numvalue=(int)get4bytes(data+tagdataoffset,MotorolaEndian);
		doublevalue=numvalue;
		break;
	case tagtype_rational:
		numvalue=(int)get4bytes(data+tagdataoffset,MotorolaEndian);
		numvalue2=(int)get4bytes(data+tagdataoffset+4,MotorolaEndian);
		if (numvalue2){ 
			doublevalue=numvalue;
			doublevalue/=numvalue2;
		} else
			doublevalue=0;
		break;
	case tagtype_float:
		doublevalue=(float)get4bytes(data+tagdataoffset,MotorolaEndian);
		break;
	case tagtype_double:
		numvalue=(int)get4bytes(data+tagdataoffset,MotorolaEndian);
		numvalue2=(int)get4bytes(data+tagdataoffset+4,MotorolaEndian);
		if (MotorolaEndian) {
			mix.i[1]=numvalue2;
			mix.i[0]=numvalue;
		} else {
			mix.i[0]=numvalue;
			mix.i[1]=numvalue2;
		}
		doublevalue=mix.d;
		break;
	}

	switch (FormatNeeded) {
	case ft_numeric_32:
		*(int*)FieldValue=numvalue;
		break;
	case ft_numeric_64:
		*(int64_t*)FieldValue=numvalue;
		break;
	case ft_numeric_floating:
		*(double*)FieldValue=doublevalue;
		if (TagNeeded==0x8825) {   // GPS: Alternate text!
//			if (unicode)
//				MultiByteToWideChar(CP_ACP,0,bufval,-1,(WCHAR*)((char*)FieldValue+sizeof(double)),maxlen-sizeof(double));
//			else
				strlcpy((char*)FieldValue+sizeof(double),bufval,maxlen-sizeof(double));
		} else if (TagNeeded==0x9202 || TagNeeded==0x9205) {
			// https://www.media.mit.edu/pia/Research/deepview/exif.html
			// The actual aperture value of lens when the image was taken. To convert this value to ordinary F-number(F-stop),
			// calculate this value's power of root 2 (=1.4142). For example, if value is '5', F-number is 1.4142^5 = F5.6.
			*(double*)FieldValue=pow(1.4142135623730950488016887242097,doublevalue);
		}
		break;
	case ft_date:
		replacechars(pcharvalue,':',' ');
		sscanf(pcharvalue,"%d %d %d",&Year,&Month,&Day);
		((pdateformat)FieldValue)->wYear=Year;
		((pdateformat)FieldValue)->wMonth=Month;
		((pdateformat)FieldValue)->wDay=Day;
		break;
	case ft_time:
		replacechars(pcharvalue,':',' ');
		sscanf(pcharvalue+11,"%d %d %d",&Hour,&Min,&Sec);
		((ptimeformat)FieldValue)->wHour=Hour;
		((ptimeformat)FieldValue)->wMinute=Min;
		((ptimeformat)FieldValue)->wSecond=Sec;
		break;
	case ft_multiplechoice:// special case: multiple choice
		switch(tagfound) {
		case 0x8822:  // ExposureProgram
			if (numvalue>8) numvalue=0;
			strlcpy((char*)FieldValue,ExposureProgramUnits[numvalue],maxlen-1);
			break;
		case 0x9208:// LightSource     - changed to respond to the corrected values fixed after version 1.4
			if (numvalue<=4)
				nr = numvalue;
			else if (numvalue>=9 && numvalue<=15)
				nr = numvalue-4;
			else if (numvalue>=17 && numvalue<=24)
				nr = numvalue-5;
			else if (numvalue==255)
				nr = 20;
			else
				nr = 0;
			strlcpy((char*)FieldValue,LightSourceUnits[nr],maxlen-1);
			break;
		case 0x9209: // Flash
			numvalue&=0x59; // ignore some flags   - There was a bug here in version 1.4 old value was 0x55
			if (numvalue==9)
				nr=2;
			else if (numvalue==16)
				nr=3;
			else if (numvalue==24)
				nr=4;
			else if (numvalue==25)
				nr=5;
			else if (numvalue==73)
				nr=6;
			else if (numvalue==89)
				nr=7;
			else if (numvalue & 1)
				nr=1;
			else
				nr=0;
			strlcpy((char*)FieldValue,FlashUnits[nr],maxlen-1);
			break;
		case 0x9207: //MeteringMode
			if (numvalue==255)
				numvalue = 7;
			else if (numvalue>7)
				numvalue=7;
			strlcpy((char*)FieldValue,MeteringModeUnits[numvalue],maxlen-1);
			break;
		case 0x112:  // Orientation
			if (numvalue>8) numvalue=0;
			strlcpy((char*)FieldValue,OrientationUnits[numvalue],maxlen-1);
			break;
		case 0x128: // Resolution Unit
			if (numvalue==3)
				numvalue=1;
			else
				numvalue=0;
			strlcpy((char*)FieldValue,ResolutionUnits[numvalue],maxlen-1);
			break;
		case 0x213:      // YCbCrPositioning
		    if (numvalue==2)
				numvalue=1;
			else
				numvalue=0;
			strlcpy((char*)FieldValue,YCbCrUnits[numvalue],maxlen-1);
			break;
		case 0xA217:		//Sensing Method
			if (numvalue<=0 || numvalue==6 || numvalue>8 )
				numvalue=0;
			else
				numvalue--;
			strlcpy((char*)FieldValue,SensingMethodUnits[numvalue],maxlen-1);
			break;
		case 0xA402:		//Exposure Mode
			if (numvalue<0 || numvalue>2  )
				numvalue=0;
			strlcpy((char*)FieldValue,ExposureModeUnits[numvalue],maxlen-1);
			break;
		case 0xA403:		//White Balance
			if (numvalue<0 || numvalue>1 )
				numvalue=0;
			strlcpy((char*)FieldValue,WhiteBalanceUnits[numvalue],maxlen-1);
			break;
		case 0xA406:		//Scene Capture
			if (numvalue<0 || numvalue>3 )
				numvalue=0;
			strlcpy((char*)FieldValue,SceneCaptureUnits[numvalue],maxlen-1);
			break;
		case 0xA407:		//Gain Control
			if (numvalue<0 || numvalue>4 )
				numvalue=0;
			strlcpy((char*)FieldValue,GainControlUnits[numvalue],maxlen-1);
			break;
		case 0xA408:		//Contrast
			if (numvalue<0 || numvalue>2 )
				numvalue=0;
			strlcpy((char*)FieldValue,ContrastUnits[numvalue],maxlen-1);
			break;
		case 0xA409:		//Saturation
			if (numvalue<0 || numvalue>2 )
				numvalue=0;
			strlcpy((char*)FieldValue,SaturationUnits[numvalue],maxlen-1);
			break;
		case 0xA40A:		//Sharpness
			if (numvalue<0 || numvalue>2 )
				numvalue=0;
			strlcpy((char*)FieldValue,SharpnessUnits[numvalue],maxlen-1);
			break;
		case 0xA40C:		//SubjectDistance
			if (numvalue<0 || numvalue>3 )
				numvalue=0;
			strlcpy((char*)FieldValue,SubjectDistanceUnits[numvalue],maxlen-1);
			break;
		case 0x0103:
			found=false;
			for (j=0;j<COUNTOF(CompressionNr);j++)
			{
				if (CompressionNr[j]==numvalue) {
					strlcpy((char*)FieldValue,CompressionName[j],maxlen-1);
					found=true;
					break;
				}
			}
			if (!found) {
				strlcpy((char*)FieldValue,CompressionName[COUNTOF(CompressionNr)],maxlen-1);
			}
			break;		
		case 0x927C:		//MakerNote
			switch(FieldIndex){
				case canon_start:					// Macro Mode
					numvalue--;
					if (numvalue<0 || numvalue>1)
						numvalue=1;
					strlcpy((char*)FieldValue,CanonMacroModeUnits[numvalue],maxlen-1);
					break;
				case canon_start+1:					// Flash mode
					if(numvalue==16)
						numvalue=7;
					if(numvalue<0 || numvalue>7)
						numvalue=1;
					strlcpy((char*)FieldValue,CanonFlashModeUnits[numvalue],maxlen-1);
					break;
				case canon_start+2:					// Continuous Mode
					if(numvalue<0 || numvalue>1)
						numvalue=0;
					strlcpy((char*)FieldValue,CanonContinousUnits[numvalue],maxlen-1);
					break;
				case canon_start+3:			// Focus Mode
					if(numvalue<0 || numvalue>6)
						numvalue=0;
					strlcpy((char*)FieldValue,CanonFocusModeUnits[numvalue],maxlen-1);
					break;
				case canon_start+4:			// Image Size
					if(numvalue<0 || numvalue>2)
						numvalue=0;
					strlcpy((char*)FieldValue,CanonImageSizeUnits[numvalue],maxlen-1);
					break;
				case canon_start+5:			// Easy shooting
					if(numvalue<0 || numvalue>11)
						numvalue=0;
					strlcpy((char*)FieldValue,CanonEasyShootingUnits[numvalue],maxlen-1);
					break;
				case canon_start+6:			// Digital Zoom
					if(numvalue<0 || numvalue>2)
						numvalue=0;
					strlcpy((char*)FieldValue,CanonDigitalZoomUnits[numvalue],maxlen-1);
					break;
				case canon_start+7:		// Contrast
				case canon_start+8:		// Saturation
				case canon_start+9:		// Sharpness
					if(numvalue==65535)
						numvalue=2;
					if(numvalue<0 || numvalue>2 )
						numvalue=0;
					strlcpy((char*)FieldValue,CanonCSSUnits[numvalue],maxlen-1);
					break;
				case canon_start+10:		// ISO speed
					if(numvalue>=15 && numvalue<=19)
						numvalue-=14;
					else
						numvalue=0;
					strlcpy((char*)FieldValue,CanonISOSpeedUnits[numvalue],maxlen-1);
					break;
				case canon_start+11:		// Metering Mode
					if(numvalue>=3 && numvalue<=5)
						numvalue-=3;
					else
						numvalue=0;
					strlcpy((char*)FieldValue,CanonMeteringModeUnits[numvalue],maxlen-1);
					break;
				case canon_start+12:		// Focus Type
					if(numvalue==3)
						numvalue=2;
					else if(numvalue==8)
						numvalue=3;
					else if (numvalue!=0 && numvalue!=1)
						numvalue=1;
					strlcpy((char*)FieldValue,CanonFocusTypeUnits[numvalue],maxlen-1);
					break;
				case canon_start+13:			// Auto Focus Select
					if(numvalue>=12288 && numvalue<=12292)
						numvalue-=12288;
					else
						numvalue=0;
					strlcpy((char*)FieldValue,CanonAFSelectUnits[numvalue],maxlen-1);
					break;
				case canon_start+14:			// Exposure Mode 
					if(numvalue>5 || numvalue<0)
						numvalue=0;
					strlcpy((char*)FieldValue,CanonExposureModeUnits[numvalue],maxlen-1);				
					break;
				case canon_start+15:			// Flash Activity
					if(numvalue>1 || numvalue<0)
						numvalue=0;
					strlcpy((char*)FieldValue,CanonFlashActivityUnits[numvalue],maxlen-1);				
					break;
				case canon_start+16:			// White Balance
					if(numvalue>6 || numvalue<0)
						numvalue=0;
					strlcpy((char*)FieldValue,CanonWhiteBalanceUnits[numvalue],maxlen-1);				
					break;
				case canon_start+17:			// Flash bias
					switch(numvalue){
						case (0xffc0): numvalue=0; break;
						case (0xffcc): numvalue=1; break;
						case (0xffd0): numvalue=2; break;
						case (0xffd4): numvalue=3; break;
						case (0xffe0): numvalue=4; break;
						case (0xffec): numvalue=5; break;
						case (0xfff0): numvalue=6; break;
						case (0xfff4): numvalue=7; break;
						case (0x0000): numvalue=8; break;
						case (0x000c): numvalue=9; break;
						case (0x0010): numvalue=10; break;
						case (0x0014): numvalue=11; break;
						case (0x0020): numvalue=12; break;
						case (0x002c): numvalue=13; break;
						case (0x0030): numvalue=14; break;
						case (0x0034): numvalue=15; break;
						case (0x0040): numvalue=16; break;
						default: numvalue=8;
					}
					strlcpy((char*)FieldValue,CanonFlashBiasUnits[numvalue],maxlen-1);				
					break;
			}
			break;
 
		default:
			strlcpy((char*)FieldValue,pcharvalue,maxlen-1);
		}
		break;
	case ft_string:
		if (tagtype==tagtype_pchar)
			strlcpy((char*)FieldValue,pcharvalue,maxlen-1);
		else if (tagtype==tagtype_float || tagtype==tagtype_double)
			sprintf((char*)FieldValue,"%G",doublevalue);
		else if (tagtype==tagtype_rational) {
		// To do: better handling of values like 16666/1000000
			if (numvalue==0 || numvalue2==0)
				strcpy((char*)FieldValue,"0");
			else if (tagfound==0x9201) { // ExposureTimeFraction by APEX value, 2^xxxxx
	            double test = exp(log((double)2)*((double)numvalue/numvalue2));
		        sprintf((char*)FieldValue,"1/%.0f",test);
			} else
				sprintf((char*)FieldValue,"%d/%d",numvalue,numvalue2);
		} else if (tagtype==tagtype_urational) {
			if (numvalue==0 || numvalue2==0)
				strcpy((char*)FieldValue,"0");
			else if (tagfound==0x829a) { // ExposureTimeFraction
				if (((unsigned int)numvalue)<=((unsigned int)numvalue2)) { // Normalize
					double dval = ((double)numvalue2)/(unsigned int)numvalue;
					unsigned int ival = ((unsigned int)numvalue2)/((unsigned int)numvalue);
					double diff=fabs((dval-ival)/ival);
					if (diff<0.01)
					{
						numvalue2 = ival;
						numvalue = 1;
					}
	               if (numvalue == numvalue2)
						sprintf((char*)FieldValue,"1");
		           else
						sprintf((char*)FieldValue,"%u/%u",(unsigned int)numvalue,(unsigned int)numvalue2);
				}
				else // numvalue>numvalue2, show as float
				{
					sprintf((char*)FieldValue,"%.2f",((double)(unsigned int)numvalue)/(unsigned int)numvalue2);
				}
			}
			else
				sprintf((char*)FieldValue,"%u/%u",numvalue,numvalue2);
		} else
			sprintf((char*)FieldValue,"%d",numvalue);
		break;
	default:
		return false;
	}
	return true;
}

int DCPCALL ContentGetValue(char* FileName,int FieldIndex,int UnitIndex,void* FieldValue,int maxlen,int flags)
{
	int f;
	DWORD bytesread;
	int length;
	char data[64*1024];   // 64k so also Nikon EXIF data fits in
	char FileName2[MAX_PATH];
	char exifheader[4];

	char* p=strrchr(FileName,'/');
	if (p)
		p=strrchr(p,'.');  // get extension
	if (!p) return ft_fileerror;
	if (!(strcasecmp(p,".jpg")==0 || strcasecmp(p,".jpeg")==0 || strcasecmp(p,".tiff")==0 || strcasecmp(p,".tif")==0
       || strcasecmp(p,".jpe")==0 || strcasecmp(p,".crw")==0  || strcasecmp(p,".thm")==0 || strcasecmp(p,".cr2")==0
       || strcasecmp(p,".dng")==0 || strcasecmp(p,".nef")==0 || strcasecmp(p,".orf")==0 || strcasecmp(p,".cr3")==0
       || strcasecmp(p,".raw")==0 || strcasecmp(p,".rw2")==0 || strcasecmp(p,".arw")==0 || strcasecmp(p,".pef")==0
	   || strcasecmp(p,".raf")==0))
		return ft_fileerror;

	strlcpy(FileName2,FileName,MAX_PATH-1);
	if (strcasecmp(p,".crw")==0)
	{
	   strcpy(FileName2+strlen(FileName2)-3,"thm");
	} 
	
	f=open(FileName2, O_RDONLY);
	if (f==INVALID_HANDLE_VALUE)
		return ft_fileerror;

	bytesread = read(f,data,12);
	strcpy(exifheader,"\xFF\xE1");
	// First two bytes always: FF D8 (start of image)
	// Next two bytes: A Tag like FF E0 = JFIF or FF E1 = Exif

	// Is the file in CR2 format? If yes, decode it directly as a TIFF file!
	if (strncmp(data,"\x49\x49",2)==0 || strncmp(data,"\x4D\x4D",2)==0) {
		length=sizeof(data);
		lseek(f,0,SEEK_SET);
	} else if (strncmp(data+4,"ftypcrx",7)==0) {
		DWORD headersize=get4bytes(data,true);
		if (lseek(f,headersize,SEEK_SET) == (off_t)-1) {
			close(f);
			return ft_fileerror;
		}
		bytesread = read(f,data,8);
		if (strncmp(data+4,"moov",4)!=0) {
			close(f);
			return ft_fileerror;
		}
		bytesread = read(f,data,24);   // uuid header inside the moov header, contains all metadata. It's 8+16 bytes long
		if (strncmp(data+4,"uuid",4)!=0) {
			close(f);
			return ft_fileerror;
		}
		DWORD uuiddatasize=get4bytes(data,true);
		DWORD lastpos=0;
		while(1) {      // find all EXIF headers within the UUID header!
			bytesread = read(f,data,8);
			headersize=get4bytes(data,true);
			lastpos+=headersize;
			if (strncmp(data+4,"CMT",3)==0 && headersize>=16 && headersize<=64*1024) {
				bytesread = read(f,data,headersize-8);
				if (ParseTiffFile(f,data,headersize-8,tagtable[FieldIndex],alternatetagtable[FieldIndex],fieldtypes[FieldIndex],FieldIndex,FieldValue,maxlen,(flags & 32768)==0,UnitIndex)) {
					close(f);
					return fieldtypes[FieldIndex];
				}
			} else if (headersize<8 || (lseek(f,headersize-8,SEEK_CUR) == (off_t)-1)) {
				break;
			}
			if (lastpos>=uuiddatasize)
				break;
		}
		close(f);
		return ft_fileerror;
	} else if (strncmp(data,"FUJIFILMCCD-",12)==0) {  // we only read the first 12 characters, it would be FUJIFILMCCD-RAW!
		bytesread = read(f,data,28+32+24);
		DWORD offset=get4bytes(data+28+32+24-12,true);
		if (lseek(f,offset,SEEK_SET) == (off_t)-1) {
			close(f);
			return ft_fileerror;
		}
		bytesread = read(f,data,12);
		if (strncmp(data,"\xFF\xD8\xFF\xE1",3)!=0 ||   // start directly with Exif?
			strncmp(data+6,"Exif",4)!=0) {
			close(f);
			return ft_fileerror;
		}
		length=256*(byte)data[4] + (byte)data[5];
	} else if (strncmp(data,"\xFF\xD8\xFF\xE1",3)!=0 ||   // start directly with Exif?
		strncmp(data+6,"Exif",4)!=0) {
		// no -> skip all other JPG headers!
		int jfifheadersize;
		if (strncmp(data,"\xFF\xD8\xFF",3)!=0) {
			close(f);
			return ft_fileerror;
		}
		// It's a JFIF file, parse it!
		jfifheadersize=256*(byte)data[4]+(byte)data[5];
		jfifheadersize-=8;  // 8 bytes of JFIF data already read
		int datasearched=12+jfifheadersize;
		
		while(1) {      // skip unneeded headers!
			if (lseek(f,jfifheadersize,SEEK_CUR) == (off_t)-1) {
				close(f);
				return ft_fileerror;
			}
			// Read next header
			bytesread = read(f,data,10);
			if (bytesread<=0) {
				close(f);
				return ft_fileerror;
			}
			// The first byte in JPG files is always 0xFF - if not, break.
			if ((unsigned char)exifheader[0]==0xFF && (unsigned char)data[0]!=0xFF) {
				close(f);
				return ft_fileerror;
			}
			if	((unsigned char)data[0]==0xFF && (unsigned char)data[1]==0xD9) {
				close(f);
				return ft_fileerror;
			}
			// Skip any APP0 extension headers
			if (strncmp(data,exifheader,2)!=0) {
				jfifheadersize=(WORD)(256*(byte)data[2])+(byte)data[3];
				jfifheadersize-=8;  // 6 bytes of JFIF data already read

				datasearched+=10+jfifheadersize;

				if (jfifheadersize<=-10)  // only continue if there is data at all!
					break;
			} else
				break;
		}
		// Now look for Exif header
		bool headergood=strncmp(data,exifheader,2)==0 &&
			(exifheader[0]==0x69 || strncmp(data+4,"Exif",4)==0);
		if (!headergood) {
			// Some stupid programs create an exif header inside an exif header on each save step!
			while (strncmp(data,"\xFF\xE1",2)==0 &&
				strncmp(data+4,"\xFF\xE1",2)==0) {
				lseek(f,-6,SEEK_CUR);
				// Read next header
				bytesread = read(f,data,10);
				headergood=strncmp(data,"\xFF\xE1",2)==0 &&
					strncmp(data+4,"Exif",4)==0;
			}
			if (!headergood) {
	 			close(f);
				return ft_fileerror;
			}
		}
		length=256*(byte)data[2] + (byte)data[3];
	} else
		length=256*(byte)data[4] + (byte)data[5];

	bytesread = read(f,data,length-8);
	if (ParseTiffFile(f,data,length-8,tagtable[FieldIndex],alternatetagtable[FieldIndex],fieldtypes[FieldIndex],FieldIndex,FieldValue,maxlen,(flags & 32768)==0,UnitIndex)) {
		close(f);
		return fieldtypes[FieldIndex];
	} else {
		close(f);
		return ft_fieldempty;
	}
}

void DCPCALL ContentSetDefaultParams(ContentDefaultParamStruct* dps)
{

}


