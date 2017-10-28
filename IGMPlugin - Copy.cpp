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

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
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
#define CONNECTED (g_pTradeDesk && g_pTradeDesk->IsLoggedIn() != 0)
#define APIKEY 2985
#define MAX_STRING 1024

/////////////////////////////////////////////////////////////
//using namespace FXCore;
//ICoreAutPtr g_pCore = 0;	// Trading desktop
//ITradeDeskAutPtr g_pTradeDesk = 0;
static BOOL g_bCoreInstance = FALSE;
static BOOL g_bMarketOpen = FALSE;
static int g_nLoggedIn = 0;
//_bstr_t g_bsAcctID = "";
//_bstr_t g_bsAccount = "";
static HINSTANCE hinstLib

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

int CallAPI(char * URL, std::string& json)
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
		id = http_send(URL, 0, "");
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

		if (counter > 3)break;
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

	return 1;
}

int CanSubscribe(std::string& Asset)
{
	char Url[256];
	sprintf(Url, "http://www.alphavantage.co/query?function=TIME_SERIES_INTRADAY&symbol=%s&interval=1min&outputsize=compact&apikey=%d", Asset.c_str(), APIKEY);
	std::string content;
	if (!CallAPI(Url, content))
		return 0;

	picojson::value v;
	std::string err = picojson::parse(v, content);
	if (!err.empty())
	{
		BrokerError(err.c_str());
		return 0;
	}

	// check if the type of the value is "object"
	if (!v.is<picojson::object>())
	{
		BrokerError("BrokerHistory2 JSON is not an object");
		return 0;
	}

	int nTick = 0;


	const picojson::value::object& objJson = v.get<picojson::object>();
	if (objJson.begin()->first == "Error Message")
	{
		//BrokerError(objJson.begin()->second.to_str().c_str());
		BrokerError("Can't subscribe Asset.");
		return 0;
	}
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
	HINSTANCE hinstLib=LoadDLL();
	BROKEROPEN ProcAdd;
	int ret = 0;

	(FARPROC&)BrokerError = fpError;
	(FARPROC&)BrokerProgress = fpProgress;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKEROPEN)GetProcAddress(hinstLib, "BrokerOpen");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret=(ProcAdd)(Name, fpError, fpProgress);
		}

		// Free the DLL module.
		FreeLibrary(hinstLib);
	}

	strcat(Name, " Wrap");
	return ret;
}



DLLFUNC int BrokerHistory2(char* Asset, DATE tStart, DATE tEnd, int nTickMinutes, int nTicks, T6* ticks)
{

	//1 Min 23/03/2017 to 06/04/2017
	//5 Min 25/01/2017 to 06/04/2017
	//15 Min 25/01/2017 to 06/04/2017
	//30 Min 25/01/2017 to 06/04/2017
	//60 Min 25/01/2017 to 06/04/2017
	//D1  from 1997

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
	sprintf(Url, "http://www.alphavantage.co/query?function=%s&symbol=%s&interval=%dmin&outputsize=%s&apikey=%d", TFARR[tf], sAsset.c_str(), interval, "full", APIKEY);


	
	std::string content;
	if(!CallAPI(Url, content))
	{
		BrokerError("Error calling API");
		return 0;
	}


	//https://github.com/kazuho/picojson
	picojson::value v;
	std::string err = picojson::parse(v, content);
	if (!err.empty()) 
	{
		BrokerError(err.c_str());
		return 0;
	}

	// check if the type of the value is "object"
	if (!v.is<picojson::object>()) 
	{
		BrokerError("BrokerHistory2 JSON is not an object");
		return 0;
	}

	int nTick = 0;
	try
	{

		const picojson::value::object& objJson = v.get<picojson::object>();
		if (objJson.begin()->first == "Error Message")
		{
			BrokerError(objJson.begin()->second.to_str().c_str());
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
		return 0;
	}


	return nTick;
}

//////////////////////////////////////////////////////////////////
//http://www.alphavantage.co/documentation/    "http://www.alphavantage.co/query?function=TIME_SERIES_INTRADAY&symbol=MSFT&interval=1min&outputsize=300&apikey=2985"

//https://bitbucket.org/yarosla/nxjson
//https://github.com/zserge/jsmn

DLLFUNC int BrokerAsset(char* Asset, double* pPrice, double* pSpread, double *pVolume, double *pPip, double *pPipCost, double *pMinAmount,
	double *pMargin, double *pRollLong, double *pRollShort)
{


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
	sprintf(Url, "http://www.alphavantage.co/query?function=%s&symbol=%s&interval=%dmin&outputsize=%s&apikey=%d", TFARR[tf], sAsset.c_str(), interval, "compact", APIKEY);
	
	std::string content;
	if (!CallAPI(Url, content))
	{
		BrokerError("Error calling API");
		return 0;
	}



	//https://github.com/kazuho/picojson
	picojson::value v;
	std::string err = picojson::parse(v, content);
	if (!err.empty()) {
		BrokerError(err.c_str());
		return 0;
	}

	// check if the type of the value is "object"
	if (!v.is<picojson::object>()) 
	{
		BrokerError("BrokerAsset JSON is not an object");
		return 0;
	}


	try
	{
		const picojson::value::object& objJson = v.get<picojson::object>();
		if (objJson.begin()->first == "Error Message")
		{
			BrokerError(objJson.begin()->second.to_str().c_str());
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
	return 1;

}

/////////////////////////////////////////////////////////////////////
DLLFUNC int BrokerTime(DATE *pTimeGMT)
{
	HINSTANCE hinstLib = LoadDLL();
	BROKERTIME ProcAdd;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERTIME)GetProcAddress(hinstLib, "BrokerTime");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(pTimeGMT);
		}

		// Free the DLL module.
		FreeLibrary(hinstLib);
	}

	return ret;
}



DLLFUNC int BrokerAccount(char* Account, double *pdBalance, double *pdTradeVal, double *pdMarginVal)
{

	HINSTANCE hinstLib = LoadDLL();
	BROKERACCOUNT ProcAdd;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERACCOUNT)GetProcAddress(hinstLib, "BrokerAccount");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(Account, pdBalance, pdTradeVal, pdMarginVal);
		}

		// Free the DLL module.
		FreeLibrary(hinstLib);
	}

	return ret;
}

DLLFUNC int BrokerBuy(char* Asset, int nAmount, double dStopDist, double *pPrice)
{

	HINSTANCE hinstLib = LoadDLL();
	BROKERBUY ProcAdd;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERBUY)GetProcAddress(hinstLib, "BrokerBuy");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(Asset, nAmount, dStopDist, pPrice);
		}

		// Free the DLL module.
		FreeLibrary(hinstLib);
	}

	return ret;
}

// returns negative amount when the trade was closed
DLLFUNC int BrokerTrade(int nTradeID, double *pOpen, double *pClose, double *pRoll, double *pProfit)
{
	HINSTANCE hinstLib = LoadDLL();
	BROKERTRADE ProcAdd;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERTRADE)GetProcAddress(hinstLib, "BrokerTrade");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(nTradeID, pOpen, pClose, pRoll, pProfit);
		}

		// Free the DLL module.
		FreeLibrary(hinstLib);
	}

	return ret;
}

DLLFUNC int BrokerSell(int nTradeID, int nAmount)
{

	HINSTANCE hinstLib = LoadDLL();
	BROKERSELL ProcAdd;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERSELL)GetProcAddress(hinstLib, "BrokerSell");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)( nTradeID,  nAmount);
		}

		// Free the DLL module.
		FreeLibrary(hinstLib);
	}
	return ret;
}

// 0 = test, 1 = relogin, 2 = login, -1 = logout
DLLFUNC int BrokerLogin(char* User, char* Pwd, char* Type, char* Account)
{
	///TODO Remove
#ifdef _DEBUG
	return 1;
#endif

	HINSTANCE hinstLib = LoadDLL();
	BROKERLOGIN ProcAdd;
	int ret = 0;

	if (hinstLib != NULL)
	{
		ProcAdd = (BROKERLOGIN)GetProcAddress(hinstLib, "BrokerLogin");
		// If the function address is valid, call the function.
		if (NULL != ProcAdd)
		{
			ret = (ProcAdd)(User, Pwd, Type, Account);
		}

		// Free the DLL module.
		FreeLibrary(hinstLib);
	}

	return ret;
}
