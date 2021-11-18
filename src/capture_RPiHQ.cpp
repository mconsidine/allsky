#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <sys/time.h>
//#include <time.h>
#include <unistd.h>
#include <string.h>
//#include <sys/types.h>
#include <errno.h>
#include <string>
#include <iomanip>
#include <cstring>
#include <sstream>
//include <iostream>
//#include <cstdio>
#include <tr1/memory>
//#include <ctime>
#include <stdlib.h>
#include <signal.h>
#include <fstream>
#include <stdarg.h>

// new includes (MEAN)
#include "include/RPiHQ_raspistill.h"
#include "include/mode_RPiHQ_mean.h"

using namespace std;

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

#define US_IN_MS 1000                     // microseconds in a millisecond
#define MS_IN_SEC 1000                    // milliseconds in a second
#define US_IN_SEC (US_IN_MS * MS_IN_SEC)  // microseconds in a second

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------

char const *fileName		= "image.jpg";
bool bMain					= true;
//ol bDisplay = false;
std::string dayOrNight;
int numExposures			= 0;	// how many valid pictures have we taken so far?

// Some command-line and other option definitions needed outside of main():
bool tty					= false;	// are we on a tty?
#define NOT_SET				  -1		// signifies something isn't set yet
#define DEFAULT_NOTIFICATIONIMAGES 1
int notificationImages		= DEFAULT_NOTIFICATIONIMAGES;

//bool bSavingImg = false;

raspistillSetting myRaspistillSetting;
modeMeanSetting myModeMeanSetting;

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------

char debugText[500];		// buffer to hold debug messages
int debugLevel = 0;

/**
 * Helper function to display debug info
**/
// [[gnu::format(printf, 2, 3)]]
void Log(int required_level, const char *fmt, ...)
{
    if (debugLevel >= required_level) {
		char msg[8192];
		snprintf(msg, sizeof(msg), "%s", fmt);
		va_list va;
		va_start(va, fmt);
		vfprintf(stdout, msg, va);
		va_end(va);
	}
}

// Return the string for the specified color, or "" if we're not on a tty.
char const *c(char const *color)
{
	if (tty)
	{
		return(color);
	}
	else
	{
		return("");
	}
}

// Return the numeric time.
timeval getTimeval()
{
    timeval curTime;
    gettimeofday(&curTime, NULL);
    return(curTime);
}

// Format a numeric time as a string.
char *formatTime(timeval t, char const *tf)
{
    static char TimeString[128];
    strftime(TimeString, 80, tf, localtime(&t.tv_sec));
    return(TimeString);
}

// Return the current time as a string.  Uses both functions above.
char *getTime(char const *tf)
{
    return(formatTime(getTimeval(), tf));
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
	size_t start_pos = 0;
	while((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
	return str;
}

std::string exec(const char *cmd)
{
	std::tr1::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
	if (!pipe)
		return "ERROR";
	char buffer[128];
	std::string result = "";
	while (!feof(pipe.get()))
	{
		if (fgets(buffer, 128, pipe.get()) != NULL)
		{
			result += buffer;
		}
	}
	return result;
}
/*
void *Display(void *params)
{
	cv::Mat *pImg = (cv::Mat *)params;
	cvNamedWindow("video", 1);
	while (bDisplay)
	{
		cvShowImage("video", pImg);
		cvWaitKey(100);
	}
	cvDestroyWindow("video");
	printf("Display thread over\n");
	return (void *)0;
}
*/

// Exit the program gracefully.
void closeUp(int e)
{
	static int closingUp = 0;		// indicates if we're in the process of exiting.
	// For whatever reason, we're sometimes called twice, but we should only execute once.
	if (closingUp) return;

	closingUp = 1;

	// If we're not on a tty assume we were started by the service.
	// Unfortunately we don't know if the service is stopping us, or restarting us.
	// If it was restarting there'd be no reason to copy a notification image since it
	// will soon be overwritten.  Since we don't know, always copy it.
	if (notificationImages) {
		system("scripts/copy_notification_image.sh NotRunning &");
		// Sleep to give it a chance to print any messages so they (hopefully) get printed
		// before the one below.  This is only so it looks nicer in the log file.
		sleep(3);
	}

	printf("     ***** Stopping AllSky *****\n");
	exit(e);
}

void IntHandle(int i)
{
	bMain = false;
	closeUp(0);
}

// A user error was found.  Wait for the user to fix it.
void waitToFix(char const *msg)
{
    printf("**********\n");
    printf("%s\n", msg);
    printf("*** After fixing, ");
    if (tty)
        printf("restart allsky.sh.\n");
    else
        printf("restart the allsky service.\n");
    if (notificationImages)
        system("scripts/copy_notification_image.sh Error &");
    sleep(5);	// give time for image to be copied
    printf("*** Sleeping until you fix the problem.\n");
    printf("**********\n");
    sleep(100000);	// basically, sleep forever until the user fixes this.
}

// Calculate if it is day or night
void calculateDayOrNight(const char *latitude, const char *longitude, const char *angle)
{
	char sunwaitCommand[128];

	// Log data.  Don't need "exit" or "set".
	sprintf(sunwaitCommand, "sunwait poll angle %s %s %s", angle, latitude, longitude);
	dayOrNight = exec(sunwaitCommand);

	// RMu, I have no clue what this does...
	dayOrNight.erase(std::remove(dayOrNight.begin(), dayOrNight.end(), '\n'), dayOrNight.end());

	if (dayOrNight != "DAY" && dayOrNight != "NIGHT")
	{
		sprintf(debugText, "*** ERROR: dayOrNight isn't DAY or NIGHT, it's '%s'\n", dayOrNight.c_str());
		waitToFix(debugText);
		closeUp(2);
	}
}

// Calculate how long until nighttime.
int calculateTimeToNightTime(const char *latitude, const char *longitude, const char *angle)
{
    std::string t;
    char sunwaitCommand[128];	// returns "hh:mm, hh:mm" (sunrise, sunset)
    sprintf(sunwaitCommand, "sunwait list angle %s %s %s | awk '{print $2}'", angle, latitude, longitude);
    t = exec(sunwaitCommand);
    t.erase(std::remove(t.begin(), t.end(), '\n'), t.end());

    int h=0, m=0, secs;
    sscanf(t.c_str(), "%d:%d", &h, &m);
    secs = (h*60*60) + (m*60);

    char *now = getTime("%H:%M");
    int hNow=0, mNow=0, secsNow;
    sscanf(now, "%d:%d", &hNow, &mNow);
    secsNow = (hNow*60*60) + (mNow*60);

    // Handle the (probably rare) case where nighttime is tomorrow
    if (secsNow > secs)
    {
        return(secs + (60*60*24) - secsNow);
    }
    else
    {
        return(secs - secsNow);
    }
}

// write value to log file
void writeToLog(int val)
{
	std::ofstream outfile;

	// Append value to the logfile
	outfile.open("log.txt", std::ios_base::app);
	outfile << val;
	outfile << "\n";
}

// Build capture command to capture the image from the HQ camera
int RPiHQcapture(int asiAutoFocus, int asiAutoExposure, int asiExposure, int asiAutoGain, int asiAutoAWB, double asiGain, int bin, double asiWBR, double asiWBB, int asiRotation, int asiFlip, float saturation, int asiBrightness, int quality, const char* fileName, int time, int showDetails, const char* ImgText, int fontsize, int fontcolor, int background, int darkframe, int preview, int width, int height)
{
	Log(3, "capturing image in file %s\n", fileName);

	// Ensure no process is still running.
	string kill = "ps -ef | grep 'raspistill' | grep -v color | awk '{print $2}' | xargs kill -9 1> /dev/null 2>&1";
	char kcmd[kill.length() + 1];		// Define char variable
	strcpy(kcmd, kill.c_str());			// Convert command to character variable

	Log(4, "Kill command: %s\n", kcmd);
	system(kcmd);						// Stop any currently running process

	stringstream ss;

	// Define command line.
	string command = "raspistill";
	ss << fileName;
	command += " --output '" + ss.str() + "'";
	command += " --thumb none --burst -st";

	// --timeout (in MS) determines how long the video will run before it takes a picture.
	if (preview)
	{
		stringstream wh;
		wh << width << "," << height;
		command += " --timeout 5000";
		command += " --preview '0,0," + wh.str() + "'";	// x,y,width,height
	}
	else
	{
		ss.str("");
		// Daytime auto-exposure pictures don't need a very long --timeout since the exposures are
		// normally short so the camera can home in on the correct exposure quickly.
		if (asiAutoExposure)
		{
			if (myModeMeanSetting.mode_mean)
			{
				// We do our own auto-exposure so no need to wait at all.
				ss << 1;
			}
			else if (dayOrNight == "DAY")
			{
				ss << 1000;
			}
			else	// NIGHT
			{
				// really could use longer but then it'll take forever for pictures to appear
				ss << 10000;
			}
		}
		else
		{
			// Manual exposure shots don't need any time to home in since we're specifying the time.
			ss << 10;
		}
		command += " --timeout " + ss.str();
		command += " --nopreview";
	}

	if (bin > 3) 	{
		bin = 3;
	}
	else if (bin < 1) 	{
		bin = 1;
	}

	// https://www.raspberrypi.com/documentation/accessories/camera.html#raspistill
	// Mode   Size         Aspect Ratio  Frame rates  FOV      Binning/Scaling
	// 0      automatic selection
	// 1      2028x1080    169:90        0.1-50fps    Partial  2x2 binned
	// 2      2028x1520    4:3           0.1-50fps    Full     2x2 binned      <<< bin==2
	// 3      4056x3040    4:3           0.005-10fps  Full     None            <<< bin==1
	// 4      1332x990     74:55         50.1-120fps  Partial  2x2 binned      <<< else 
	//
  // TODO: please change gui description !

	if (bin==1)	{
		command += " --mode 3";
	}
	else if (bin==2) 	{
		command += " --mode 2  --width 2028 --height 1520";
	}
	else 	{
		command += " --mode 4 --width 1012 --height 760";
	}

	if (asiExposure < 1)
	{
		asiExposure = 1;
	}
	else if (asiExposure > 200000000)
	{
		// https://www.raspberrypi.org/documentation/raspbian/applications/camera.md : HQ (IMX477) 	200000000 (i.e. 200s)
		asiExposure = 200000000;
	}

	// Check if automatic determined exposure time is selected

	if (asiAutoExposure)
	{
		if (myModeMeanSetting.mode_mean) {
			ss.str("");
			ss << myRaspistillSetting.shutter_us;
			command += " --exposure off";
			command += " --shutter " + ss.str();
		} else {
			command += " --exposure auto";
		}
	}
	// Set exposure time
	else if (asiExposure)
	{
		ss.str("");
		ss << asiExposure;
		command += " --exposure off";
		command += " --shutter " + ss.str();
	}

	if (asiAutoFocus)
	{
		command += " --focus";
	}

	// Check if auto gain is selected
	if (asiAutoGain)
	{
		if (myModeMeanSetting.mode_mean) {
			ss.str("");
			ss << myRaspistillSetting.analoggain;
			command += " --analoggain " + ss.str();

			if (myRaspistillSetting.digitalgain > 1.0) {
				ss.str("");
				ss << myRaspistillSetting.digitalgain;
				command += " --digitalgain " + ss.str();
			}
		}
		else {
			command += " --analoggain 1";	// 1 effectively makes it autogain
		}
	}
	else {							// Is manual gain
		if (asiGain < 1.0) {
			asiGain = 1.0;
		}
		else if (asiGain > 16.0) {
			asiGain = 16.0;
		}
		ss.str("");
		ss << asiGain;
		command += " --analoggain " + ss.str();
	}

	if (myModeMeanSetting.mode_mean) {
	   	stringstream Str_ExposureTime;
   		stringstream Str_Reinforcement;
   		Str_ExposureTime <<  myRaspistillSetting.shutter_us;
		Str_Reinforcement << myRaspistillSetting.analoggain;
		
   		command += " --exif IFD0.Artist=li_" + Str_ExposureTime.str() + "_" + Str_Reinforcement.str();
	}

	// White balance
	if (asiWBR < 0.1) {
		asiWBR = 0.1;
	}
	else if (asiWBR > 10) {
		asiWBR = 10;
	}
	if (asiWBB < 0.1) {
		asiWBB = 0.1;
	}
	else if (asiWBB > 10) {
		asiWBB = 10;
	}

	// Check if R and B component are given
	if (myModeMeanSetting.mode_mean) {
		// support asiAutoAWB, asiWBR and asiWBB
		if (asiAutoAWB) {
  			command += " --awb auto";
		}
		else {
			ss.str("");
			ss << asiWBR << "," << asiWBB;
			command += " --awb off";
			command += " --awbgains " + ss.str();
		}
	}
	else if (! asiAutoAWB) {
		ss.str("");
		ss << asiWBR << "," << asiWBB;
		command += " --awb off";
		command += " --awbgains " + ss.str();
	}
	// Use automatic white balance
	else {
		command += " --awb auto";
	}

	// Check if rotation is at least 0 degrees
	if (asiRotation != 0 && asiRotation != 90 && asiRotation != 180 && asiRotation != 270)
	{
		asiRotation = 0;
	}

	// check if rotation is needed
	if (asiRotation!=0) {
		ss.str("");
		ss << asiRotation;
		command += " --rotation "  + ss.str();
	}

	// Check if flip is selected
	if (asiFlip == 1 || asiFlip == 3)
	{
		// Set horizontal flip
		command += " --hflip";
	}
	if (asiFlip == 2 || asiFlip == 3)
	{
		// Set vertical flip
		command += " --vflip";
	}

	if (saturation < -100.0)
	{
		saturation = -100.0;
	}
	else if (saturation > 100.0)
	{
		saturation = 100.0;
	}

	if (saturation)
	{
		ss.str("");
		ss << saturation;
		command += " --saturation "+ ss.str();
	}

	// Brightness
	if (asiBrightness < 0)
	{
		asiBrightness = 0;
	}
	else if (asiBrightness > 100)
	{
		asiBrightness = 100;
	}

	// check if brightness setting is set
	if (asiBrightness!=50)
	{
		ss.str("");
		ss << asiBrightness;
		command += " --brightness " + ss.str();
	}

	if (quality < 0)
	{
		quality = 0;
	}
	else if (quality > 100)
	{
		quality = 100;
	}

	ss.str("");
	ss << quality;
	command += " --quality " + ss.str();

	if (!darkframe) {
		if (showDetails)
			command += " -a 1104";

		if (time==1)
			command += " -a 1036";

		if (strcmp(ImgText, "") != 0) {
			ss.str("");
			ss << ImgText; 
			if (myModeMeanSetting.debugLevel > 0) {
				ss << " shutter:" << myRaspistillSetting.shutter_us 
				<< " gain:"	<< myRaspistillSetting.analoggain;
				if (myModeMeanSetting.debugLevel  > 1 ) {
					ss << " (li-" << __TIMESTAMP__ 
					<< ") br:" << myRaspistillSetting.brightness 
					<< " WBR:" << asiWBR 
					<< " WBB:" << asiWBB;
				}
			}
			command += " -a \"" + ss.str() + "\"";
		}

		if (fontsize < 6)
			fontsize = 6;
		else if (fontsize > 160)
			fontsize = 160;

		ss.str("");
		ss << fontsize;

		if (fontcolor < 0)
			fontcolor = 0;
		else if (fontcolor > 255)
			fontcolor = 255;

		std::stringstream C;
		C  << std::setfill ('0') << std::setw(2) << std::hex << fontcolor;

		if (background < 0)
			background = 0;
		else if (background > 255)
			background = 255;

		std::stringstream B;
		B  << std::setfill ('0') << std::setw(2) << std::hex << background;

		command += " -ae " + ss.str() + ",0x" + C.str() + ",0x8080" + B.str();
	}

	// Define char variable
	char cmd[command.length() + 1];

	// Convert command to character variable
	strcpy(cmd, command.c_str());

	Log(1, "Capture command: %s\n", cmd);

	// Execute the command.
	return(system(cmd));
}

// Simple function to make flags easier to read for humans.
char const *yes = "1 (yes)";
char const *no  = "0 (no)";
char const *yesNo(int flag)
{
    if (flag)
        return(yes);
    else
        return(no);
}


//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	tty = isatty(fileno(stdout)) ? true : false;
	signal(SIGINT, IntHandle);
	signal(SIGTERM, IntHandle);	// The service sends SIGTERM to end this program.
/*
	int fontname[] = { CV_FONT_HERSHEY_SIMPLEX,        CV_FONT_HERSHEY_PLAIN,         CV_FONT_HERSHEY_DUPLEX,
					   CV_FONT_HERSHEY_COMPLEX,        CV_FONT_HERSHEY_TRIPLEX,       CV_FONT_HERSHEY_COMPLEX_SMALL,
					   CV_FONT_HERSHEY_SCRIPT_SIMPLEX, CV_FONT_HERSHEY_SCRIPT_COMPLEX };
	int fontnumber = 0;
	int iStrLen;
	int iTextX = 15, iTextY = 25;
*/
	char const *ImgText   = "";
	char const *param     = "";
	double fontsize       = 32;
/*
	int linewidth         = 1;
	int outlinefont       = 0;
*/
	int fontcolor         = 255;
	int background        = 0;
/*
	int smallFontcolor[3] = { 0, 0, 255 };
	int linetype[3]       = { CV_AA, 8, 4 };
	int linenumber        = 0;
	char buf[1024]    = { 0 };
	char bufTime[128] = { 0 };
	char bufTemp[128] = { 0 };
*/
	int width             = 0;
	int height            = 0;
	int dayBin            = 1;
	int nightBin          = 2;
	int currentBin        = NOT_SET;
	int asiDayExposure    = 32;	// milliseconds
	int asiNightExposure  = 60000000;
	int currentExposure   = NOT_SET;
	int asiNightAutoExposure = 0;
	int asiDayAutoExposure= 1;
	int currentAutoExposure = 0;
	int asiAutoFocus      = 0;
	double asiNightGain   = 4.0;
	double asiDayGain     = 1.0;
	double currentGain    = NOT_SET;
	int asiNightAutoGain  = 0;
	int asiDayAutoGain    = 0;
	int currentAutoGain   = NOT_SET;
	int asiAutoAWB        = 0;
	int nightDelay        = 10;   // Delay in milliseconds. Default is 10ms
	int dayDelay          = 15000; // Delay in milliseconds. Default is 15 seconds
	int currentDelay      = NOT_SET;
	double asiWBR         = 2.5;
	double asiWBB         = 2;
	float saturation      = 0.0;
	int asiDayBrightness  = 50;
	int asiNightBrightness= 50;
	int currentBrightness = NOT_SET;
	int asiFlip           = 0;
	int asiRotation       = 0;
	char const *latitude  = "52.57N";
	char const *longitude = "4.70E";
	char const *angle     = "0"; // angle of the sun with the horizon (0=sunset, -6=civil twilight, -12=nautical twilight, -18=astronomical twilight)
	int preview           = 0;
	int time              = 0;
	int showDetails       = 0;
	int darkframe         = 0;
	int daytimeCapture    = 0;
	int help              = 0;
	int quality           = 90;

	int i;
	//id *retval;
	bool endOfNight    = false;
	//hread_t hthdSave = 0;
	int retCode;

	//-------------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------------
	setlinebuf(stdout);   // Line buffer output so entries appear in the log immediately.
	printf("\n");
	printf("%s ******************************************\n", c(KGRN));
	printf("%s *** Allsky Camera Software v0.8 | 2021 ***\n", c(KGRN));
	printf("%s ******************************************\n\n", c(KGRN));
	printf("\%sCapture images of the sky with a Raspberry Pi and a RPi HQ camera\n", c(KGRN));
	printf("\n");
	printf("%sAdd -h or -help for available options\n", c(KYEL));
	printf("\n");
	printf("\%sAuthor: ", c(KNRM));
	printf("Thomas Jacquin - <jacquin.thomas@gmail.com>\n\n");
	printf("\%sContributors:\n", c(KNRM));
	printf("-Knut Olav Klo\n");
	printf("-Daniel Johnsen\n");
	printf("-Yang and Sam from ZWO\n");
	printf("-Robert Wagner\n");
	printf("-Michael J. Kidd - <linuxkidd@gmail.com>\n");
	printf("-Rob Musquetier\n");	
	printf("-Eric Claeys\n");
	printf("\n");

	// The newer "allsky.sh" puts quotes around arguments so we can have spaces in them.
	// If you are running the old allsky.sh, set this to false:
	bool argumentsQuoted = true;

	if (argc > 1)
	{
		Log(4, "Found %d parameters...\n", argc - 1);

		for (i = 1; i <= argc - 1; i++)
		{
			Log(4, "Processing argument: %s\n\n", argv[i]);

			if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			{
				help = 1;
			}
			else if (strcmp(argv[i], "-width") == 0)
			{
				width = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-height") == 0)
			{
				height = atoi(argv[++i]);
			}
/*
			else if (strcmp(argv[i], "-type") == 0)
			{
				Image_type = atoi(argv[++i]);
			}
*/
			else if (strcmp(argv[i], "-quality") == 0)
			{
				quality = atoi(argv[++i]);
			}
			// check for old names as well - the "||" part is the old name
			else if (strcmp(argv[i], "-nightexposure") == 0 || strcmp(argv[i], "-exposure") == 0)
			{
				asiNightExposure = atoi(argv[++i]) * US_IN_MS;
			}

			else if (strcmp(argv[i], "-nightautoexposure") == 0 || strcmp(argv[i], "-autoexposure") == 0)
			{
				asiNightAutoExposure = atoi(argv[++i]);
			}

			else if (strcmp(argv[i], "-autofocus") == 0)
			{
				asiAutoFocus = atoi(argv[++i]);
			}
			// xxxx Day gain isn't settable by the user.  Should it be?
			else if (strcmp(argv[i], "-nightgain") == 0 || strcmp(argv[i], "-gain") == 0)
			{
				asiNightGain = atof(argv[++i]);
			}
			else if (strcmp(argv[i], "-nightautogain") == 0 || strcmp(argv[i], "-autogain") == 0)
			{
				asiNightAutoGain = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-saturation") == 0)
			{
				saturation = atof(argv[++i]);
			}
			else if (strcmp(argv[i], "-brightness") == 0)// old "-brightness" applied to day and night
			{
				asiDayBrightness = atoi(argv[++i]);
				asiNightBrightness = asiDayBrightness;
			}
			else if (strcmp(argv[i], "-daybrightness") == 0)
			{
				asiDayBrightness = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-nightbrightness") == 0)
			{
				asiNightBrightness = atoi(argv[++i]);
			}
 			else if (strcmp(argv[i], "-daybin") == 0)
      {
      	dayBin = atoi(argv[++i]);
      }
			else if (strcmp(argv[i], "-nightbin") == 0 || strcmp(argv[i], "-bin") == 0)
			{
				nightBin = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-nightdelay") == 0 || strcmp(argv[i], "-delay") == 0)
			{
				nightDelay = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-daydelay") == 0 || strcmp(argv[i], "-daytimeDelay") == 0)
			{
				dayDelay = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-awb") == 0)
			{
				asiAutoAWB = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-wbr") == 0)
			{
				asiWBR = atof(argv[++i]);
			}
			else if (strcmp(argv[i], "-wbb") == 0)
			{
				asiWBB = atof(argv[++i]);
			}
			else if (strcmp(argv[i], "-mean-value") == 0)
			{
				myModeMeanSetting.mean_value = std::min(1.0,std::max(0.0,atof(argv[i + 1])));
				myModeMeanSetting.mode_mean = true;
				i++;
			}
			else if (strcmp(argv[i], "-mean-threshold") == 0)
			{
				myModeMeanSetting.mean_threshold = std::min(0.1,std::max(0.0001,atof(argv[i + 1])));
				myModeMeanSetting.mode_mean = true;
				i++;
			}
			else if (strcmp(argv[i], "-mean-p0") == 0)
			{
				myModeMeanSetting.mean_p0 = std::min(50.0,std::max(0.0,atof(argv[i + 1])));
				myModeMeanSetting.mode_mean = true;
				i++;
			}
			else if (strcmp(argv[i], "-mean-p1") == 0)
			{
				myModeMeanSetting.mean_p1 = std::min(50.0,std::max(0.0,atof(argv[i + 1])));
				myModeMeanSetting.mode_mean = true;
				i++;
			}
      else if (strcmp(argv[i], "-mean-p2") == 0)
			{
				myModeMeanSetting.mean_p2 = std::min(50.0,std::max(0.0,atof(argv[i + 1])));
				myModeMeanSetting.mode_mean = true;
				i++;
			}

			// Check for text parameter
			else if (strcmp(argv[i], "-text") == 0)
			{
				if (argumentsQuoted)
				{
					ImgText = argv[++i];
				}
				else
				{
					// Get first param
					param = argv[i + 1];

					// Space character
					const char *space = " ";

					// Temporary text buffer
					char buffer[1024]; // <- danger, only storage for 1024 characters.

					// First word flag
					int j = 0;

					// Loop while next parameter doesn't start with a - character
					while (strncmp(param, "-", 1) != 0)
					{
						// Copy Text into buffer
						strncpy(buffer, ImgText, sizeof(buffer));

						// Add a space after each word (skip for first word)
						if (j)
							strncat(buffer, space, sizeof(buffer));

						// Add parameter
						strncat(buffer, param, sizeof(buffer));

						// Copy buffer into ImgText variable
						ImgText = buffer;

						// Flag first word is entered
						j = 1;

						// Get next parameter
						param = argv[++i];
					}
				}
			}
/*
			else if (strcmp(argv[i], "-textx") == 0)
			{
				iTextX = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-texty") == 0)
			{
				iTextY = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-fontname") == 0)
			{
				fontnumber = atoi(argv[++i]);
			}
*/
			else if (strcmp(argv[i], "-background") == 0)
			{
				background = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-fontcolor") == 0)
			{
				fontcolor = atoi(argv[++i]);
			}
/*
			else if (strcmp(argv[i], "-smallfontcolor") == 0)
			{
				if (argumentsQuoted)
				{
					sscanf(argv[++i], "%d %d %d", &smallFontcolor[0], &smallFontcolor[1], &smallFontcolor[2]);
				}
				else
				{
					smallFontcolor[0] = atoi(argv[++i]);
					smallFontcolor[1] = atoi(argv[++i]);
					smallFontcolor[2] = atoi(argv[++i]);
				}
			}
			else if (strcmp(argv[i], "-fonttype") == 0)
			{
				linenumber = atoi(argv[++i]);
			}
*/
			else if (strcmp(argv[i], "-fontsize") == 0)
			{
				fontsize = atof(argv[++i]);
			}
/*
			else if (strcmp(argv[i], "-fontline") == 0)
			{
				linewidth = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-outlinefont") == 0)
			{
				outlinefont = atoi(argv[++i]);
				if (outlinefont != 0)
					outlinefont = 1;
			}
*/
			else if (strcmp(argv[i], "-rotation") == 0)
			{
				asiRotation = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-flip") == 0)
			{
				asiFlip = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-filename") == 0)
			{
				fileName = (argv[++i]);
			}
			else if (strcmp(argv[i], "-latitude") == 0)
			{
				latitude = argv[++i];
			}
			else if (strcmp(argv[i], "-longitude") == 0)
			{
				longitude = argv[++i];
			}
			else if (strcmp(argv[i], "-angle") == 0)
			{
				angle = argv[++i];
			}
			else if (strcmp(argv[i], "-preview") == 0)
			{
				preview = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-debuglevel") == 0)
			{
				debugLevel = atoi(argv[++i]);
				myModeMeanSetting.debugLevel = debugLevel;
			}
			else if (strcmp(argv[i], "-showTime") == 0 || strcmp(argv[i], "-time") == 0)
			{
				time = atoi(argv[++i]);
			}

			else if (strcmp(argv[i], "-darkframe") == 0)
			{
				darkframe = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-showDetails") == 0)
			{
				showDetails = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-daytime") == 0)
			{
				daytimeCapture = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-notificationimages") == 0)
			{
				notificationImages = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "-tty") == 0)
			{
				tty = atoi(argv[++i]) ? true : false;
			}
		}
	}

	if (help == 1)
	{
		printf("%sAvailable Arguments:\n", c(KYEL));
		printf(" -width                             - Default = Camera Max Width\n");
		printf(" -height                            - Default = Camera Max Height\n");
		printf(" -nightexposure                     - Default = 5000000 - Time in us (equals to 5 sec)\n");
		printf(" -nightautoexposure                 - Default = 0 - Set to 1 to enable auto Exposure\n");
		printf(" -autofocus                         - Default = 0 - Set to 1 to enable auto Focus\n");
		printf(" -nightgain                         - Default = 4.0 (1.0 - 16.0)\n");
		printf(" -nightautogain                     - Default = 0 - Set to 1 to enable auto Gain at night\n");
		printf(" -saturation                        - Default = 0 (-100 to 100)\n");
		printf(" -brightness                        - Default = 50 (0 to 100)\n");
		printf(" -awb                               - Default = 0 - Auto White Balance (0 = off)\n");
		printf(" -wbr                               - Default = 2 - White Balance Red  (0 = off)\n");
		printf(" -wbb                               - Default = 2 - White Balance Blue (0 = off)\n");
		printf(" -daybin                            - Default = 1 - binning OFF (1x1), 2 = 2x2, 3 = 3x3 binning\n");
		printf(" -nightbin                          - Default = 1 - same as -daybin but for nighttime\n");
		printf(" -nightdelay                        - Default = 10 - Delay between images in milliseconds - %d = 1 sec.\n", MS_IN_SEC);
		printf(" -daydelay                          - Default = 5000 - Delay between images in milliseconds - 5000 = 5 sec.\n");
		printf(" -type = Image Type                 - Default = 0 - 0 = RAW8,  1 = RGB24,  2 = RAW16\n");
		printf(" -quality                           - Default = 70%%, 0%% (poor) 100%% (perfect)\n");
		printf(" -filename                          - Default = image.jpg\n");
		printf(" -rotation                          - Default = 0 degrees - Options 0, 90, 180 or 270\n");
		printf(" -flip                              - Default = 0 - 0 = Orig, 1 = Horiz, 2 = Verti, 3 = Both\n");
		printf("\n");
		printf(" -text                              - Default =      - Character/Text Overlay. Use Quotes.  Ex. -c "
			   "\"Text Overlay\"\n");
//		printf(" -textx                             - Default = 15   - Text Placement Horizontal from LEFT in Pixels\n");
//		printf(" -texty = Text Y                    - Default = 25   - Text Placement Vertical from TOP in Pixels\n");
//		printf(" -fontname = Font Name              - Default = 0    - Font Types (0-7), Ex. 0 = simplex, 4 = triplex, 7 = script\n");
		printf(" -fontcolor = Font Color            - Default = 255  - Text gray scale color  (0 - 255)\n");
		printf(" -background= Font Color            - Default = 0  - Backgroud gray scale color (0 - 255)\n");
//		printf(" -smallfontcolor = Small Font Color - Default = 0 0 255  - Text red (BGR)\n");
//		printf(" -fonttype = Font Type              - Default = 0    - Font Line Type,(0-2), 0 = AA, 1 = 8, 2 = 4\n");
		printf(" -fontsize                          - Default = 32  - Text Font Size (range 6 - 160, 32 default)\n");
//		printf(" -fontline                          - Default = 1    - Text Font Line Thickness\n");
		printf("\n");
		printf("\n");
		printf(" -latitude                          - Default = 60.7N (Whitehorse)   - Latitude of the camera.\n");
		printf(" -longitude                         - Default = 135.05W (Whitehorse) - Longitude of the camera\n");
		printf(" -angle                             - Default = -6 - Angle of the sun below the horizon. -6=civil "
			   "twilight, -12=nautical twilight, -18=astronomical twilight\n");
		printf("\n");
		printf(" -preview                           - set to 1 to preview the captured images. Only works with a Desktop Environment\n");
		printf(" -time                              - Adds the time to the image.\n");
		printf(" -darkframe                         - Set to 1 to grab dark frame and cover your camera\n");
		printf(" -showDetails                       - Set to 1 to display the metadata on the image\n");
		printf(" -notificationimages                - Set to 1 to enable notification images, for example, 'Camera is off during day'.\n");
		printf(" -debuglevel                        - Default = 0. Set to 1,2 or 3 for more debugging information.\n");
	  printf("%s", c(KBLU));
		printf(" -mean-value                        - Default = 0.3 Set mean-value and activates exposure control\n");
		printf("                                    - info: Auto-Gain should be set (gui)\n");
		printf("                                    -       -autoexposure should be set (extra parameter)\n");
		printf("                                    - config.sh\n");
		printf("                                    -       # Additional Capture parameters.  Run 'capture_RPiHQ -h' to see the options.\n");
		printf("                                    -       CAPTURE_EXTRA_PARAMETERS='--mean-value 0.3 -autoexposure 1'\n"); 
		printf(" -mean-threshold                    - Default = 0.01 Set mean-value and activates exposure control\n");
		printf(" -mean-p0                           - Default = 5.0, be careful changing these values, ExposureChange (Steps) = p0 + p1 * diff + (p2*diff)^2\n");
		printf(" -mean-p1                           - Default = 20.0\n");
		printf(" -mean-p2                           - Default = 45.0\n");
	  printf("%s", c(KNRM));
		printf("%sUsage:\n", c(KRED));
		printf(" ./capture_RPiHQ -width 640 -height 480 -nightexposure 5000000 -gamma 50 -nightbin 1 -filename Lake-Laberge.JPG\n\n");
		exit(0);
	}

	printf("%s", c(KNRM));

	int iMaxWidth = 4096;
	int iMaxHeight = 3040;
	double pixelSize = 1.55;

	printf("- Resolution: %dx%d\n", iMaxWidth, iMaxHeight);
	printf("- Pixel Size: %1.2fmicrons\n", pixelSize);
	printf("- Supported Bin: 1x, 2x and 3x\n");

	if (darkframe)
	{
		// To avoid overwriting the optional notification inage with the dark image,
		// during dark frames we use a different file name.
		fileName = "dark.jpg";
	}

	//-------------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------------

	printf("%s", c(KGRN));
	printf("\nCapture Settings:\n");
	printf(" Resolution (before any binning): %dx%d\n", width, height);
	printf(" Quality: %d\n", quality);
	printf(" Exposure (night): %1.0fms\n", round(asiNightExposure / US_IN_MS));
	printf(" Auto Exposure (night): %s\n", yesNo(asiNightAutoExposure));
	printf(" Auto Focus: %s\n", yesNo(asiAutoFocus));
	printf(" Gain (night): %1.2f\n", asiNightGain);
	printf(" Auto Gain (night): %s\n", yesNo(asiNightAutoGain));
	printf(" Brightness (day): %d\n", asiDayBrightness);
	printf(" Brightness (night): %d\n", asiNightBrightness);
	printf(" Saturation: %.2f\n", saturation);
	printf(" Auto White Balance: %s\n", yesNo(asiAutoAWB));
	printf(" WB Red: %1.2f\n", asiWBR);
	printf(" WB Blue: %1.2f\n", asiWBB);
	printf(" Binning (day): %d\n", dayBin);
	printf(" Binning (night): %d\n", nightBin);
	printf(" Delay (day): %dms\n", dayDelay);
	printf(" Delay (night): %dms\n", nightDelay);
	printf(" Text Overlay: %s\n", ImgText);
//	printf(" Text Position: %dpx left, %dpx top\n", iTextX, iTextY);
//	printf(" Font Name:  %d\n", fontname[fontnumber]);
	printf(" Font Color: %d\n", fontcolor);
	printf(" Font Background Color: %d\n", background);
//	printf(" Small Font Color: %d , %d, %d\n", smallFontcolor[0], smallFontcolor[1], smallFontcolor[2]);
//	printf(" Font Line Type: %d\n", linetype[linenumber]);
	printf(" Font Size: %1.1f\n", fontsize);
//	printf(" Font Line: %d\n", linewidth);
//	printf(" Outline Font : %s\n", yesNo(outlinefont));
	printf(" Rotation: %d\n", asiRotation);
	printf(" Flip Image: %d\n", asiFlip);
	printf(" Filename: %s\n", fileName);
	printf(" Latitude: %s\n", latitude);
	printf(" Longitude: %s\n", longitude);
	printf(" Sun Elevation: %s\n", angle);
	printf(" Preview: %s\n", yesNo(preview));
	printf(" Debug Level: %d\n", debugLevel);
	printf(" Time: %s\n", yesNo(time));
	printf(" Show Details: %s\n", yesNo(showDetails));
	printf(" Darkframe: %s\n", yesNo(darkframe));
	printf(" Notification Images: %s\n", yesNo(notificationImages));
	printf(" Mode Mean: %s\n", yesNo(myModeMeanSetting.mode_mean));
	if (myModeMeanSetting.mode_mean) {
		printf("    Value: %1.3f\n", myModeMeanSetting.mean_value);
		printf("    Threshold: %1.3f\n", myModeMeanSetting.mean_threshold);
		printf("    p0: %1.3f\n", myModeMeanSetting.mean_p0);
		printf("    p1: %1.3f\n", myModeMeanSetting.mean_p1);
		printf("    p2: %1.3f\n", myModeMeanSetting.mean_p2);
	}

	// Show selected camera type
	printf(" Camera: Raspberry Pi HQ camera\n");

	printf("%s", c(KNRM));

	// Initialization
	std::string lastDayOrNight;
	int displayedNoDaytimeMsg = 0; // Have we displayed "not taking picture during day" message, if applicable?

	while (bMain)
	{
		printf("\n");

		// Find out if it is currently DAY or NIGHT
		calculateDayOrNight(latitude, longitude, angle);

// Next line is present for testing purposes
// dayOrNight.assign("NIGHT");

		lastDayOrNight = dayOrNight;

// Next lines are present for testing purposes
Log(3, "Daytimecapture: %d\n", daytimeCapture);

		if (myModeMeanSetting.mode_mean && numExposures > 0) {
  			RPiHQcalcMean(fileName, asiNightExposure, asiNightGain, myRaspistillSetting, myModeMeanSetting);
		}

		printf("\n");

		if (darkframe) {
			// We're doing dark frames so turn off autoexposure and autogain, and use
			// nightime gain, delay, exposure, and brightness to mimic a nightime shot.
			currentAutoExposure = 0;
			currentAutoGain = 0;
			currentGain = asiNightGain;
			currentDelay = nightDelay;
			currentExposure = asiNightExposure;
			currentBrightness = asiNightBrightness;
			currentBin = nightBin;

 			Log(0, "Taking dark frames...\n");
			if (notificationImages) {
				system("scripts/copy_notification_image.sh DarkFrames &");
			}
		}

		else if (dayOrNight == "DAY")
		{
			if (endOfNight == true)		// Execute end of night script
			{
				system("scripts/endOfNight.sh &");

				// Reset end of night indicator
				endOfNight = false;

				displayedNoDaytimeMsg = 0;
			}

// Next line is present for testing purposes
// daytimeCapture = 1;

			// Check if images should not be captured during day-time
			if (daytimeCapture != 1)
			{
				// Only display messages once a day.
				if (displayedNoDaytimeMsg == 0) {
					if (notificationImages) {
						system("scripts/copy_notification_image.sh CameraOffDuringDay &");
					}
					Log(0, "It's daytime... we're not saving images.\n%s\n",
						tty ? "Press Ctrl+C to stop" : "Stop the allsky service to end this process.");
					displayedNoDaytimeMsg = 1;

					// sleep until almost nighttime, then wake up and sleep a short time
					int secsTillNight = calculateTimeToNightTime(latitude, longitude, angle);
					sleep(secsTillNight - 10);
				}
				else
				{
					// Shouldn't need to sleep more than a few times before nighttime.
					sleep(5);
				}

				// No need to do any of the code below so go back to the main loop.
				continue;
			}

			// Images should be captured during day-time
			else
			{
				// Inform user
				char const *x;
				if (numExposures > 0)	// so it's easier to see in log file
					x = "\n==========\n";
				else
					x = "";
				Log(0, "%s=== Starting daytime capture ===\n%s", x, x);

				// set daytime settings
				currentAutoExposure = asiDayAutoExposure;
				currentAutoGain = asiDayAutoGain;
				currentGain = asiDayGain;
				currentDelay = dayDelay;
				currentExposure = asiDayExposure;
				currentBrightness = asiDayBrightness;
				currentBin = dayBin;

				// Inform user
				Log(0, "Saving %d ms exposed images with %d seconds delays in between...\n\n", currentExposure * US_IN_MS, currentDelay / MS_IN_SEC);
			}
		}

		else	// NIGHT
		{
			char const *x;
			if (numExposures > 0)	// so it's easier to see in log file
				x = "\n==========\n";
			else
				x = "";
			Log(0, "%s=== Starting nighttime capture ===\n%s", x, x);

			// Set nighttime settings
			currentAutoExposure = asiNightAutoExposure;
			currentAutoGain = asiNightAutoGain;
			currentGain = asiNightGain;
			currentDelay = nightDelay;
			currentExposure = asiNightExposure;
			currentBrightness = asiNightBrightness;
			currentBin = nightBin;

			// Inform user
			Log(0, "Saving %d seconds exposure images with %d ms delays in between...\n\n", (int)round(currentExposure / US_IN_SEC), nightDelay);
		}

		// Adjusting variables for chosen binning
		width  = iMaxWidth / currentBin;
		height = iMaxHeight / currentBin;
//		iTextX    = iTextX / currentBin;
//		iTextY    = iTextY / currentBin;
//		fontsize  = fontsize / currentBin;
//		linewidth = linewidth / currentBin;

		// Inform user
		if (tty)
			printf("Press Ctrl+Z to stop\n\n");	// xxx ECC: Ctrl-Z stops a process, it doesn't kill it
		else
			printf("Stop the allsky service to end this process.\n\n");

		// Wait for switch day time -> night time or night time -> day time
		while (bMain && lastDayOrNight == dayOrNight)
		{
			// Inform user
			Log(0, "Capturing & saving image...\n");

			// Capture and save image
			retCode = RPiHQcapture(asiAutoFocus, currentAutoExposure, currentExposure, currentAutoGain, asiAutoAWB, currentGain, currentBin, asiWBR, asiWBB, asiRotation, asiFlip, saturation, currentBrightness, quality, fileName, time, showDetails, ImgText, fontsize, fontcolor, background, darkframe, preview, width, height);
			if (retCode == 0)
			{
				numExposures++;
			}
			else
			{
				printf(" >>> Unable to take picture, return code=%d\n", (retCode >> 8));
				Log(1, "  > Sleeping from failed exposure: %d seconds\n", currentDelay / MS_IN_SEC);
				usleep(currentDelay * US_IN_MS);
				continue;
			}

			// Check for night time
			if (dayOrNight == "NIGHT")
			{
				// Preserve image during night time
				system("scripts/saveImageNight.sh &");
			}
			else
			{
				// Upload and resize image when configured
				system("scripts/saveImageDay.sh &");
			}

			if (myModeMeanSetting.mode_mean) {
				RPiHQcalcMean(fileName, asiNightExposure, asiNightGain, myRaspistillSetting, myModeMeanSetting);
				if (myModeMeanSetting.debugLevel >= 2)
					printf("asiExposure: %d shutter: %1.4f s quickstart: %d\n", asiNightExposure, (double) myRaspistillSetting.shutter_us / 1000000.0, myModeMeanSetting.quickstart);
				if (myModeMeanSetting.quickstart) {
					currentDelay = 1000;
				}
				else if ((dayOrNight == "NIGHT")) {
					currentDelay = (asiNightExposure / 1000) - (int) (myRaspistillSetting.shutter_us / 1000.0) + nightDelay;
				}
				else {
					currentDelay = dayDelay;
				}
			}

			// Inform user
			Log(0, "Capturing & saving %s done, now wait %d seconds...\n", darkframe ? "dark frame" : "image", currentDelay / MS_IN_SEC);

			// Sleep for a moment
			usleep(currentDelay * US_IN_MS);

			// Check for day or night based on location and angle
			calculateDayOrNight(latitude, longitude, angle);
		}

		// Check for night situation
		if (lastDayOrNight == "NIGHT")
		{
			// Flag end of night processing is needed
			endOfNight = true;
		}
	}

	closeUp(0);
}

