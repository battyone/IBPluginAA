// FXCMplugin.cpp : Defines the entry point for the DLL application.
// Zorro broker plugin, using the FXCM API as a 'worst case' example.
// Most broker APIs are less complex and easier to implement.

//https://github.com/uWebSockets/uWebSockets

/*
Hi,

first define the function prototypes:

int (__cdecl *http_send)(char* url, char* data, char* header) = NULL;
long (__cdecl *http_status)(int id) = NULL;
long (__cdecl *http_result)(int id,char* content,long size) = NULL;
int (__cdecl *http_free)(int id) = NULL;

Then set the function pointers:

DLLFUNC int BrokerHTTP(FARPROC fp_send,FARPROC fp_status,FARPROC fp_result,FARPROC fp_free)
{
(FARPROC&)http_send = fp_send;
(FARPROC&)http_status = fp_status;
(FARPROC&)http_result = fp_result;
(FARPROC&)http_free = fp_free;
return 1;
}

Then you can call the functions, f.i. http_send, as in the http examples in the manual.
*/
#define _CRT_SECURE_NO_WARNINGS
#include "stdafx.h"

//#define FROMFILE
//#define FAKELOGIN


INITIALIZE_EASYLOGGINGPP

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		OutputDebugStringA("DllMain, DLL_PROCESS_ATTACH\n");
		break;
	case DLL_THREAD_ATTACH:
		OutputDebugStringA("DllMain, DLL_THREAD_ATTACH\n");
		break;
	case DLL_THREAD_DETACH:
		OutputDebugStringA("DllMain, DLL_THREAD_DETACH\n");
		break;
	case DLL_PROCESS_DETACH:
		OutputDebugStringA("DllMain, DLL_PROCESS_DETACH\n");
		break;
	default:
		OutputDebugStringA("DllMain, ????\n");
		break;
	}
	return TRUE;
}


/////////////////////////////////////////////////////////////
typedef double DATE;
//#include "D:\Zorro\include\trading.h"  // enter your path to trading.h (in your Zorro folder)
#include "..\..\Zorro\include\trading.h"
//#import "C:\\Program Files\\CandleWorks\\FXOrder2Go\fxcore.dll"  // FXCM API module

#define PLUGIN_VERSION	2
#define DLLFUNC extern "C" __declspec(dllexport)
#define LOOP_MS	200	// repeat a failed command after this time (ms)
#define WAIT_MS	10000	// wait this time (ms) for success of a command
//#define CONNECTED (g_pTradeDesk && g_pTradeDesk->IsLoggedIn() != 0)

#define MAX_STRING 1024

/////////////////////////////////////////////////////////////

int(__cdecl *BrokerError)(const char *txt) = NULL;
int(__cdecl *BrokerProgress)(const int percent) = NULL;

//HTTP func pointers prototypes
int(__cdecl *http_send)(char* url, char* data, char* header) = NULL;
long(__cdecl *http_status)(int id) = NULL;
long(__cdecl *http_result)(int id, char* content, long size) = NULL;
int(__cdecl *http_free)(int id) = NULL;


//Wrapper Func pointers declarations
typedef int(__cdecl *BROKEROPEN)(LPCSTR, FARPROC, FARPROC);
typedef int(__cdecl *BROKERTIME)(DATE *);
typedef int(__cdecl *BROKERLOGIN)(LPCSTR, LPCSTR, LPCSTR, LPCSTR);
typedef int(__cdecl *BROKERHISTORY2)(LPCSTR, DATE, DATE, int, int, T6*);
typedef int(__cdecl *BROKERASSET)(LPCSTR, double *, double *, double *, double *, double *, double *, double *, double *, double *);
typedef int(__cdecl *BROKERACCOUNT)(LPCSTR, double *, double *, double *);
typedef int(__cdecl *BROKERBUY)(LPCSTR, int, double , double *);
typedef int(__cdecl *BROKERTRADE)( int, double*, double *, double*, double*);
typedef int(__cdecl *BROKERSELL)(int,int);
typedef int(__cdecl *BROKERSTOP)(int, double);
typedef double(__cdecl *BROKERCOMMAND)(int, DWORD);

static const char * TFARR[] = { "TIME_SERIES_INTRADAY", "TIME_SERIES_DAILY", "TIME_SERIES_WEEKLY", "TIME_SERIES_MONTLY" };

//Timezone info struct
#define pWin32Error(dwSysErr, sMsg )

typedef struct _REG_TZI_FORMAT
{
	LONG Bias;
	LONG StandardBias;
	LONG DaylightBias;
	SYSTEMTIME StandardDate;
	SYSTEMTIME DaylightDate;
} REG_TZI_FORMAT;

struct SymbolCacheItem
{
	std::string json;
	COleDateTime ExpiryTime;
};

#define REG_TIME_ZONES "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\"
#define REG_TIME_ZONES_LEN (sizeof(REG_TIME_ZONES)-1)
#define REG_TZ_KEY_MAXLEN (REG_TIME_ZONES_LEN + (sizeof(((TIME_ZONE_INFORMATION*)0)->StandardName)/2) -1)
#define ZONE "Eastern Standard Time" //https://msdn.microsoft.com/en-us/library/ms912391(v=winembedded.11).aspx



enum TIMEFRAME
{
	TIME_SERIES_INTRADAY=0,
	TIME_SERIES_DAILY=1,
	TIME_SERIES_WEEKLY=2,
	TIME_SERIES_MONTLY=3

};

enum INTERVAL
{
	MIN1 = 1,
	MIN5 = 5,
	MIN15 = 15,
	MIN30 = 30,
	MIN60 = 60,
	MIN1440 = 1440
};


//////////////////////// DLL Globals
static int g_nLoggedIn = 0;
static HINSTANCE hinstLib = 0;
Dictionary<std::string, SymbolCacheItem> SymbolDic;
std::string AlphaAdvantageKey;
int AlphaAdvantageIntervalMin = 0;

////////////////////////////////////////////////////////////////
HINSTANCE LoadDLL()
{
	HINSTANCE hinstLib=0;
	BOOL fFreeResult, fRunTimeLinkSuccess = FALSE;
	//hinstLib = LoadLibrary(TEXT("D:\\Zorro\\Plugin\\IB.dll"));
	hinstLib = LoadLibrary(TEXT(".\\Plugin\\IB.dll"));
	if (hinstLib != NULL)
		return hinstLib;
	else
		return 0;
}

//DATE format (OLE date/time) is a double float value, counting days since midnight 30 December 1899, while hours, minutes, and seconds are represented as fractional days. 
DATE convertTime(__time32_t t32)
{
	return (double)t32 / (24.*60.*60.) + 25569.; // 25569. = DATE(1.1.1970 00:00)
}

// number of seconds since January 1st 1970 midnight: 
__time32_t convertTime(DATE date)
{
	return (__time32_t)((date - 25569.)*24.*60.*60.);
}


//http://stackoverflow.com/questions/466071/how-do-i-get-a-specific-time-zone-information-struct-in-win32
int GetTimeZoneInformationByName(TIME_ZONE_INFORMATION *ptzi, const char StandardName[])
{
	int rc;
	HKEY hkey_tz;
	DWORD dw;
	REG_TZI_FORMAT regtzi;
	char tzsubkey[REG_TZ_KEY_MAXLEN + 1] = REG_TIME_ZONES;

	strncpy(tzsubkey + REG_TIME_ZONES_LEN, StandardName, sizeof(tzsubkey) - REG_TIME_ZONES_LEN);
	if (tzsubkey[sizeof(tzsubkey) - 1] != 0) {
		fprintf(stderr, "timezone name too long\n");
		return -1;
	}

	if (ERROR_SUCCESS != (dw = RegOpenKeyA(HKEY_LOCAL_MACHINE, tzsubkey, &hkey_tz))) {
		fprintf(stderr, "failed to open: HKEY_LOCAL_MACHINE\\%s\n", tzsubkey);
		pWin32Error(dw, "RegOpenKey() failed");
		return -1;
	}

	rc = 0;
#define X(param, type, var) \
        do if ((dw = sizeof(var)), (ERROR_SUCCESS != (dw = RegGetValueW(hkey_tz, NULL, param, type, NULL, &var, &dw)))) { \
            fprintf(stderr, "failed to read: HKEY_LOCAL_MACHINE\\%s\\%S\n", tzsubkey, param); \
            pWin32Error(dw, "RegGetValue() failed"); \
            rc = -1; \
            goto ennd; \
				        } while(0)
	X(L"TZI", RRF_RT_REG_BINARY, regtzi);
	X(L"Std", RRF_RT_REG_SZ, ptzi->StandardName);
	X(L"Dlt", RRF_RT_REG_SZ, ptzi->DaylightName);
#undef X
	ptzi->Bias = regtzi.Bias;
	ptzi->DaylightBias = regtzi.DaylightBias;
	ptzi->DaylightDate = regtzi.DaylightDate;
	ptzi->StandardBias = regtzi.StandardBias;
	ptzi->StandardDate = regtzi.StandardDate;
ennd:
	RegCloseKey(hkey_tz);
	return rc;
}

void Log(char * message, int Level)
{
#ifdef _DEBUG
	//if (Level==0)
	//LOG(INFO) << message;
	//else if (Level==1)
	//LOG(ERROR) << message;
#endif
}

int AddToCache(std::string symbol, std::string json)
{
	try
	{
		SymbolCacheItem *jsonCached = SymbolDic.TryGetValue(symbol);
		if (jsonCached)
			SymbolDic.Remove(symbol);

		SymbolCacheItem newitem;
		newitem.json = json;
		COleDateTime currTime = COleDateTime::GetCurrentTime();
		COleDateTime exptime(currTime.GetYear(), currTime.GetMonth(), currTime.GetDay(), currTime.GetHour(), currTime.GetMinute() + AlphaAdvantageIntervalMin, 0);
		CString sStart = currTime.Format(_T("%A, %B %d, %Y %H:%M:%S"));
		CString sEnd = exptime.Format(_T("%A, %B %d, %Y %H:%M:%S"));
		newitem.ExpiryTime = exptime;
		SymbolDic.Add(symbol, newitem);
		return 1;
	}
	catch (...)
	{
		Log("AddToCache Exception", 1);
		return 0;
	}
}

int GetFromCache(std::string symbol, std::string &json)
{
	try
	{
		SymbolCacheItem *jsonCached = SymbolDic.TryGetValue(symbol);
		if (jsonCached)
		{
			COleDateTime currTime = COleDateTime::GetCurrentTime();
			if (!jsonCached->ExpiryTime.GetStatus() == COleDateTime::valid)
				return 0;

			if (currTime > jsonCached->ExpiryTime)
				return 0;
			json = jsonCached->json;
			return 1;
		}
		else
			return 0;
	}
	catch (...)
	{
		Log("GetFromCache Exception", 1);
		return 0;
	}
}

void RemoveFromCache(std::string symbol)
{
	try
	{
		SymbolCacheItem *jsonCached = SymbolDic.TryGetValue(symbol);
		if (jsonCached)
			SymbolDic.Remove(symbol);
	}
	catch (...)
	{
		Log("RemoveFromCache Exception", 1);

	}
}

int CallAPI(char * URL, std::string& json, std::string symbol, bool UseCache)
{

	int id = 0;
	int length = 0;
	int counter = 0;

#ifdef FROMFILE
	//std::ifstream t("..\..\Zorro\json.txt");
	std::ifstream t("json.txt");
	std::string str((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
	json.assign(str);
	length = json.length();
#else

	while (length <= 3)
	{
		if (UseCache)
		{
			if (GetFromCache(symbol, json))
				return 1;
		}

		id = http_send(URL, 0, 0);
		if (!id)
			return 0;
		while (!http_status(id))
		{
			if (!SleepEx(100, FALSE))
				return 0; // wait for the server to reply
		}

		length = http_status(id);
		if (length <= 3)
			http_free(id);

		if (counter > 3)
		{
			BrokerError("Maximum Retry calling http_send reached.");
			Log("Maximum Retry calling http_send reached.",1);
			break;
		}
		counter++;
	}

#endif


	if (length > 3)
	{ //transfer successful?

#ifndef FROMFILE
		string content = (string)malloc(length + 1);
		http_result(id, content, length); // store price data in large string
		json.assign(content);
		free(content);
#ifdef _DEBUG
		Log(URL,0);
		//Log((char *)json.c_str(),0);
#endif
		http_free(id);
#endif
	}
	else
	{
		BrokerError("Error calling web service");
#ifndef FROMFILE
		http_free(id); //always clean up the id!
#endif
		return 0;
	}

	if (UseCache)
		AddToCache(symbol, json);
	return 1;
}



int CanSubscribe(std::string& Asset)
{
	Log("CanSubscribe IN", 0);
	char Url[256];
	sprintf(Url, "https://www.alphavantage.co/query?function=TIME_SERIES_INTRADAY&symbol=%s&interval=1min&outputsize=compact&apikey=%s", Asset.c_str(), AlphaAdvantageKey.c_str());
	std::string content;
	if (!CallAPI(Url, content, Asset,true))
		return 0;

	picojson::value v;
	std::string err = picojson::parse(v, content);
	if (!err.empty())
	{
		BrokerError(err.c_str());
		BrokerError(Asset.c_str());
		BrokerError(content.c_str());
		Log("Try Log Error if works...", 1);
		Log((char *)err.c_str(), 1);
		RemoveFromCache(Asset);
		return 0;
	}

	// check if the type of the value is "object"
	if (!v.is<picojson::object>())
	{
		BrokerError("CanSubscribe JSON is not an object");
		Log("CanSubscribe OUT", 0);
		RemoveFromCache(Asset);
		return 0;
	}

	int nTick = 0;


	const picojson::value::object& objJson = v.get<picojson::object>();
	if (objJson.begin()->first == "Error Message")
	{
		BrokerError(objJson.begin()->second.to_str().c_str());
		Log((char *)objJson.begin()->second.to_str().c_str(), 1);
		BrokerError("Can't subscribe Asset.");
		Log("CanSubscribe OUT", 0);
		RemoveFromCache(Asset);
		return 0;
	}

	Log("CanSubscribe OUT", 0);
	return 1;
}

/*
0 when the connection to the server was lost, and a new login is required.
1 when the connection is ok, but the market is closed or trade orders are not accepted.
2 when the connection is ok and the market is open for trading at least one of the subscribed assets.
*/
DLLFUNC int BrokerTime(DATE *pTimeGMT);
int IsLoggedIn()
{

#ifdef FAKELOGIN
	return 1;
#endif

	DATE date = 0;
	return BrokerTime(&date);
}


/*
[ALPHAADVANTAGE]
AlphaAdvantageKey = "????"
AlphaAdvantageIntervalMin = "15"*/
std::string ReadZorroIniConfig(std::string key)
{
	char bufDir[300];
	GetCurrentDirectoryA(300, bufDir);
	strcat(bufDir, "\\Zorro.ini");

	char buf[300];
	int success=GetPrivateProfileStringA("ALPHAADVANTAGE", key.c_str(), "", buf, 300, bufDir);
	std::string ret = std::string(buf);
	if (ret == std::string(""))
	{
		BrokerError(key.c_str());
		BrokerError(" not set in Zorro.ini");
	}

	return ret;
}

////////////////////////////////////////////////////////////////

DLLFUNC int BrokerHTTP(FARPROC fp_send, FARPROC fp_status, FARPROC fp_result, FARPROC fp_free)
{
	(FARPROC&)http_send = fp_send;
	(FARPROC&)http_status = fp_status;
	(FARPROC&)http_result = fp_result;
	(FARPROC&)http_free = fp_free;
	return 1;
}

DLLFUNC int BrokerOpen(char* Name, FARPROC fpError, FARPROC fpProgress)
{
	Log("BrokerOpen IN",0);

	HINSTANCE hinstLib=LoadDLL();
	BROKEROPEN ProcAdd=0;
	int ret = 0;

	(FARPROC&)BrokerError = fpError;
	(FARPROC&)BrokerProgress = fpProgress;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKEROPEN)GetProcAddress(hinstLib, "BrokerOpen");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(Name, fpError, fpProgress);
		}

		// Free the DLL module.
		//FreeLibrary(hinstLib);
		//hinstLib = 0;
	}
	else
		return 0;

	strcpy(Name, "IB AA Wrap");

	AlphaAdvantageKey = ReadZorroIniConfig(std::string("AlphaAdvantageKey"));
	std::string sAlphaAdvantageIntervalMin = ReadZorroIniConfig(std::string("AlphaAdvantageIntervalMin"));
	AlphaAdvantageIntervalMin = atoi(sAlphaAdvantageIntervalMin.c_str());

	Log("BrokerOpen OUT", 0);
	return ret;

}



DLLFUNC int BrokerHistory2(char* Asset, DATE tStart, DATE tEnd, int nTickMinutes, int nTicks, T6* ticks)
{
	Log("BrokerHistory2 IN", 0);
	//1 Min 23/03/2017 to 06/04/2017
	//5 Min 25/01/2017 to 06/04/2017
	//15 Min 25/01/2017 to 06/04/2017
	//30 Min 25/01/2017 to 06/04/2017
	//60 Min 25/01/2017 to 06/04/2017
	//D1  from 1997

	if (!IsLoggedIn())
		return 0;

	//Remove backslash for currencies
	std::string sAsset = std::string(Asset);
	sAsset.erase(std::remove(sAsset.begin(), sAsset.end(), '/'), sAsset.end());

	if (nTickMinutes != (int)MIN1 && nTickMinutes != (int)MIN5 && nTickMinutes != (int)MIN15 && nTickMinutes != (int)MIN30 && nTickMinutes != (int)MIN60 && nTickMinutes != (int)MIN1440)
	{
		BrokerError("TimeFrame not supported");
		return 0;
	}

	int tf = 0;
	if (nTickMinutes < 1440)
		tf = TIME_SERIES_INTRADAY;
	else
		tf = TIME_SERIES_DAILY;

	COleDateTime odtStart(tStart);
	COleDateTime odtEnd(tEnd);
	CString sStart = odtStart.Format(_T("%A, %B %d, %Y"));
	CString sEnd = odtEnd.Format(_T("%A, %B %d, %Y"));
	COleDateTimeSpan diff = odtEnd - odtStart;
	int TotalMinutes = diff.GetTotalMinutes();
	int TotalDays = diff.GetTotalDays();


	//CString Warning ="Request: " + sStart + " - " + sEnd;
	//BrokerError((LPCSTR)Warning.GetBuffer());
	/*if (TotalDays > 14 && nTickMinutes == (int)MIN1)
	{
		BrokerError("1Min Data not available for more than 14 days.");
		return 0;
	}
	else if (TotalDays > 70 && (nTickMinutes == (int)MIN5 || nTickMinutes == (int)MIN15 || nTickMinutes == (int)MIN30 || nTickMinutes == (int)MIN60 ))
	{
		BrokerError("5/15/30/60 Min Data not available for more than 70 days.");
		return 0;
	}*/



	char Url[256];
	int interval = nTickMinutes;

	int tokensize = TotalMinutes / nTickMinutes;
	//if (tokensize > nTicks)
	//	tokensize = nTicks;
	const int outputsize = tokensize;
	sprintf(Url, "https://www.alphavantage.co/query?function=%s&symbol=%s&interval=%dmin&outputsize=%s&apikey=%s", TFARR[tf], sAsset.c_str(), interval, "full", AlphaAdvantageKey.c_str());


	
	std::string content;
	if (!CallAPI(Url, content, sAsset,true))
	{
		BrokerError("Error calling API");
		Log("Error calling API", 1);
		Log("BrokerHistory2 OUT", 0);
		return 0;
	}


	//https://github.com/kazuho/picojson
	picojson::value v;
	std::string err = picojson::parse(v, content);
	if (!err.empty()) 
	{
		BrokerError(err.c_str());
		Log((char *)err.c_str(), 1);
		Log("BrokerHistory2 OUT", 0);
		return 0;
	}

	// check if the type of the value is "object"
	if (!v.is<picojson::object>()) 
	{
		BrokerError("BrokerHistory2 JSON is not an object");
		Log("BrokerHistory2 JSON is not an object", 1);
		Log("BrokerHistory2 OUT", 0);
		return 0;
	}

	int nTick = 0;
	try
	{

		const picojson::value::object& objJson = v.get<picojson::object>();
		if (objJson.begin()->first == "Error Message")
		{
			BrokerError(objJson.begin()->second.to_str().c_str());
			Log((char*)objJson.begin()->second.to_str().c_str(), 1);
			Log("BrokerHistory2 OUT", 0);
			return 0;
		}
		picojson::value::object::const_iterator iA = objJson.begin(); ++iA;//iA shoud be Time Series Object

		const picojson::value::object& objTimeSeries = iA->second.get<picojson::object>();
		picojson::value::object::const_iterator iB = objTimeSeries.begin(); ++iB;//iA shoud be Time Series Object

		const picojson::value::object& objTimeSeriesArray = iB->second.get<picojson::object>();

		T6* tick = ticks;
		//Start from the most recent quote
		for (picojson::value::object::const_iterator iC = objTimeSeries.end(); iC != objTimeSeries.begin(); --iC)
		{
			if (iC == objTimeSeries.end())
				continue;

			tick->fOpen = atof(iC->second.get("1. open").to_str().c_str());
			tick->fClose = atof(iC->second.get("4. close").to_str().c_str());
			tick->fHigh = atof(iC->second.get("2. high").to_str().c_str());
			tick->fLow = atof(iC->second.get("3. low").to_str().c_str());
			tick->fVol = atof(iC->second.get("5. volume").to_str().c_str());
			CString sTickTime = iC->first.c_str();
			COleDateTime tTickTime;
			tTickTime.ParseDateTime(sTickTime);

			/////////////////////
			//https://msdn.microsoft.com/en-us/library/ms725485(VS.85).aspx
			//http://stackoverflow.com/questions/466071/how-do-i-get-a-specific-time-zone-information-struct-in-win32

			DWORD dw;
			TIME_ZONE_INFORMATION tzi;
			dw = GetTimeZoneInformationByName(&tzi, ZONE);
			if (dw != 0) return 1;
			SYSTEMTIME utc;
			//GetSystemTime(&lt);
			SYSTEMTIME custTime =
			{
				tTickTime.GetYear(), /*WORD wYear;*/
				tTickTime.GetMonth(), /*WORD wMonth;*/
				tTickTime.GetDayOfWeek(), /*WORD wDayOfWeek;*/
				tTickTime.GetDay(), /*WORD wDay;*/
				tTickTime.GetHour(), /*WORD wHour;*/
				tTickTime.GetMinute(), /*WORD wMinute;*/
				tTickTime.GetSecond(), /*WORD wSecond;*/
				0 /*WORD wMilliseconds;*/
			};

			TzSpecificLocalTimeToSystemTime(&tzi, &custTime, &utc);
			COleDateTime cUTC(utc);
			tTickTime = cUTC;
			tick->time = tTickTime.m_dt;


			if (nTick == 0)
				tEnd = cUTC.m_dt;
			tStart = cUTC.m_dt;

			if (iC == objTimeSeries.begin())
				int xxx = nTick;

			//char buf[30];
			//sprintf(buf, "%f", (double)BrokerProgress(100 * nTick / nTicks));
			//LOG(INFO) << buf;
			if (!BrokerProgress(100 * nTick / nTicks))
				break;

			//OutputDebugStringA("\n\r");
			//OutputDebugStringA(iC->first.c_str());

			if (nTick == nTicks - 1)
				break;
			
			tick++;
			nTick++;

		}
	}
	catch (const std::exception& e)
	{
		BrokerError(e.what());
		Log("BrokerHistory2 OUT", 0);
		return 0;
	}

	Log("BrokerHistory2 OUT", 0);
	return nTick;
}

//////////////////////////////////////////////////////////////////
//http://www.alphavantage.co/documentation/    "http://www.alphavantage.co/query?function=TIME_SERIES_INTRADAY&symbol=MSFT&interval=1min&outputsize=300&apikey=2985"

//https://bitbucket.org/yarosla/nxjson
//https://github.com/zserge/jsmn

DLLFUNC int BrokerAsset(char* Asset, double* pPrice, double* pSpread, double *pVolume, double *pPip, double *pPipCost, double *pMinAmount,
	double *pMargin, double *pRollLong, double *pRollShort)
{

	Log("BrokerAsset IN Symbol: ", 0);
	Log(Asset, 0);

	if (!IsLoggedIn())
		return 0;

	//Remove backslash for currencies
	std::string sAsset = std::string(Asset);
	sAsset.erase(std::remove(sAsset.begin(), sAsset.end(), '/'), sAsset.end());

	//Subscribe the asset
	if (!pPrice)
		return CanSubscribe(sAsset);
	else
		*pPrice = 0.;

	char Url[256];
	int interval = 1;
	const int outputsize = 1500;
	int tf = TIME_SERIES_INTRADAY;
	sprintf(Url, "https://www.alphavantage.co/query?function=%s&symbol=%s&interval=%dmin&outputsize=%s&apikey=%s", TFARR[tf], sAsset.c_str(), interval, "compact", AlphaAdvantageKey.c_str());
	
	std::string content;
	picojson::value v;
	try
	{
		if (!CallAPI(Url, content, sAsset, true))
		{
			BrokerError("Error calling API");
			Log("Error calling API", 1);
			Log("BrokerAsset OUT", 0);
			RemoveFromCache(sAsset);
			return 0;
		}


		//https://github.com/kazuho/picojson
		
		std::string err = picojson::parse(v, content);
		if (!err.empty()) {
			BrokerError(err.c_str());
			BrokerError(Asset);
			BrokerError(content.c_str());
			Log("Try Log Error if works...", 1);
			Log((char *)err.c_str(), 1);
			RemoveFromCache(sAsset);
			Log("BrokerAsset OUT", 0);
			return 0;
		}

		// check if the type of the value is "object"
		if (!v.is<picojson::object>())
		{
			BrokerError("BrokerAsset JSON is not an object");
			Log("BrokerAsset JSON is not an object", 1);
			Log("BrokerAsset OUT", 0);
			RemoveFromCache(sAsset);
			return 0;
		}
	}
	catch (...)
	{
		Log("BrokerAsset Catched Exception", 1);
	}


	try
	{
		const picojson::value::object& objJson = v.get<picojson::object>();
		if (objJson.begin()->first == "Error Message")
		{
			BrokerError(objJson.begin()->second.to_str().c_str());
			Log((char *)objJson.begin()->second.to_str().c_str(),1);
			Log("BrokerAsset OUT", 0);
			RemoveFromCache(sAsset);
			return 0;
		}
		picojson::value::object::const_iterator iA = objJson.begin(); ++iA;//iA shoud be Time Series Object


		const picojson::value::object& objTimeSeries = iA->second.get<picojson::object>();
		picojson::value::object::const_iterator iB = objTimeSeries.begin(); ++iB;//iA shoud be Time Series Object

		const picojson::value::object& objTimeSeriesArray = iB->second.get<picojson::object>();
		picojson::value::object::const_iterator iC = objTimeSeries.end(); --iC;//iC shoud be the last item in the timeseries

		double dPrice = atof(iC->second.get("4. close").to_str().c_str());
		if (pPrice)
			*pPrice = dPrice;

		double volume = atof(iC->second.get("5. volume").to_str().c_str());
		if (pVolume)
			*pVolume = volume;
	}
	catch (const std::exception& e)
	{
		BrokerError(e.what());
		return 0;
	}


	//return 1 if successfull
	Log("BrokerAsset OUT", 0);
	return 1;

}

/////////////////////////////////////////////////////////////////////
DLLFUNC int BrokerTime(DATE *pTimeGMT)
{
	Log("BrokerTime IN", 0);

	BROKERTIME ProcAdd=0;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERTIME)GetProcAddress(hinstLib, "BrokerTime");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(pTimeGMT);
		}
	}

	Log("BrokerTime OUT", 0);
	return ret;
}



DLLFUNC int BrokerAccount(char* Account, double *pdBalance, double *pdTradeVal, double *pdMarginVal)
{
	Log("BrokerAccount IN", 0);

	if (!IsLoggedIn())
		return 0;

	BROKERACCOUNT ProcAdd=0;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERACCOUNT)GetProcAddress(hinstLib, "BrokerAccount");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(Account, pdBalance, pdTradeVal, pdMarginVal);
		}
	}

	Log("BrokerAccount OUT", 0);
	return ret;
}

DLLFUNC int BrokerBuy(char* Asset, int nAmount, double dStopDist, double *pPrice)
{
	Log("BrokerBuy IN", 0);

	if (!IsLoggedIn())
		return 0;

	BROKERBUY ProcAdd=0;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERBUY)GetProcAddress(hinstLib, "BrokerBuy");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(Asset, nAmount, dStopDist, pPrice);
		}
	}

	Log("BrokerBuy OUT", 0);
	return ret;
}

// returns negative amount when the trade was closed
DLLFUNC int BrokerTrade(int nTradeID, double *pOpen, double *pClose, double *pRoll, double *pProfit)
{
	Log("BrokerTrade IN", 0);

	if (!IsLoggedIn())
		return 0;

	BROKERTRADE ProcAdd=0;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERTRADE)GetProcAddress(hinstLib, "BrokerTrade");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(nTradeID, pOpen, pClose, pRoll, pProfit);
		}
	}

	Log("BrokerTrade OUT", 0);
	return ret;
}

DLLFUNC int BrokerSell(int nTradeID, int nAmount)
{
	Log("BrokerSell IN", 0);
	if (!IsLoggedIn())
		return 0;

	BROKERSELL ProcAdd=0;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERSELL)GetProcAddress(hinstLib, "BrokerSell");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)( nTradeID,  nAmount);
		}
	}

	Log("BrokerSell OUT", 0);
	return ret;
}

// 0 = test, 1 = relogin, 2 = login, -1 = logout
DLLFUNC int BrokerLogin(char* User, char* Pwd, char* Type, char* Account)
{
	Log("BrokerLogin IN",0);

#ifdef FAKELOGIN
	return 1;
#endif


	if (hinstLib == NULL)
		hinstLib = LoadDLL();


	BROKERLOGIN ProcAdd=0;


	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERLOGIN)GetProcAddress(hinstLib, "BrokerLogin");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			__try
			{
				if (g_nLoggedIn >0 && User != NULL)
					return g_nLoggedIn;

				g_nLoggedIn = (ProcAdd)(User, Pwd, Type, Account);
				if (g_nLoggedIn>0)
					BrokerError("IB AA Wrap Login Success!");

				//int FreeLibrarySuccess = 0;
				//if (g_nLoggedIn == 0)
				//{
				//	if (hinstLib != NULL)
				//	{
				//		FreeLibrarySuccess=FreeLibrary(hinstLib);
				//		hinstLib == NULL;
				//	}
				//}
				//if (FreeLibrarySuccess>0)
				//	hinstLib == NULL;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				BrokerError("IB Wrap Login FAILED!");
				//g_nLoggedIn = 0;
				//if (hinstLib != NULL)
				//{
				//	FreeLibrary(hinstLib);
				//	hinstLib == NULL;
				//}
			}


		}
	}

	Log("BrokerLogin OUT", 0);
	return g_nLoggedIn;
}

DLLFUNC int BrokerStop(int nTradeID, double dStop)
{
	Log("BrokerStop IN", 0);

	if (!IsLoggedIn())
		return 0;

	BROKERSTOP ProcAdd = 0;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERSTOP)GetProcAddress(hinstLib, "BrokerStop");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(nTradeID, dStop);
		}
	}

	Log("BrokerStop OUT", 0);
	return ret;
}

DLLFUNC double BrokerCommand(int nCommand, DWORD dwParameter)
{
	Log("BrokerCommand IN", 0);

	if (!IsLoggedIn())
		return 0;

	BROKERCOMMAND ProcAdd = 0;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERCOMMAND)GetProcAddress(hinstLib, "BrokerCommand");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(nCommand, dwParameter);
		}
	}

	Log("BrokerAccount OUT", 0);
	return ret;
}