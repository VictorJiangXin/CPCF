#include "kernel.h"
#include "file_dir.h"
#include "multi_thread.h"

#if defined(PLATFORM_WIN)
#include <Rpc.h>
#include <Psapi.h>
#include <Sddl.h>
#pragma comment(lib,"Psapi.lib")
#include <fcntl.h>
#include <Wincon.h>
#include <io.h>
#include <iostream>
#include <ShellAPI.h>
#include <wincrypt.h>
#pragma comment(lib,"Crypt32.lib")


#elif defined(PLATFORM_IOS) || defined(PLATFORM_MAC)

#include <sys/sysctl.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <sys/mman.h>
#include <uuid/uuid.h>
#include <dlfcn.h>
#include <unistd.h>

extern int _objc_get_bundle_path(char* pOut, int OutSize);
extern UINT _objc_get_screens_dim(rt::Vec2i* p, UINT co);

#define MAP_ANONYMOUS MAP_ANON

#if defined(PLATFORM_MAC)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/IOKitLib.h>
#include <sys/resource.h>

extern UINT _objc_preference_load_string(LPCSTR key, LPSTR val_out, UINT val_size);
extern void _objc_preference_save_string(LPCSTR key, LPCSTR val);
#endif


#if defined(PLATFORM_IOS)
extern int _objc_get_battery_state();
extern bool _objc_get_device_uid(char id[64]);
#endif

#elif defined(PLATFORM_ANDROID) || defined(PLATFORM_LINUX)

#ifdef PLATFORM_ANDROID
#include <sys/sysconf.h>
#include <time64.h>
#else
#endif

#ifdef PLATFORM_LINUX
#include <X11/Xlib.h>
#endif

#include <sys/utsname.h>
#include <sys/mman.h>
#include <asm/param.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fstream>

#endif


void os::HighPerformanceCounter::SetOutputUnit(LONGLONG u)
{
#if defined(PLATFORM_WIN)
	VERIFY(QueryPerformanceFrequency((LARGE_INTEGER*)&_Mul));
	LONGLONG m = 1000000000LL/_Mul/u;
	_bMul = m>0;
	if(_bMul){ _Mul = m; }
	else{ _Div = _Mul*u/1000000000LL; }
#elif defined (PLATFORM_IOS) || defined (PLATFORM_MAC)
    LONGLONG m = 1000000LL*::os::TickCount::__mach_tick_unit.numer/::os::TickCount::__mach_tick_unit.denom/u;
	_bMul = _Mul>0;
	if(_bMul){ _Mul = m; }
	else{ _Div = ::os::TickCount::__mach_tick_unit.denom*u/1000000LL/::os::TickCount::__mach_tick_unit.numer; }
#else
	_Div = u;
#endif
}

LPCSTR os::GetBuildSpecificationString()
{
	return 
	#if		defined(PLATFORM_DEBUG_BUILD)
						"Debug"
	#elif	defined(PLATFORM_RELEASE_BUILD)
						"Release"
	#endif

	#if		defined(PLATFORM_32BIT)
						"/x86"
	#elif	defined(PLATFORM_64BIT)
						"/x64"
	#endif

	#if		defined(PLATFORM_ANDROID)
						"/Andriod"
	#elif	defined(PLATFORM_IOS)
						"/iOS"
	#elif	defined(PLATFORM_MAC)
						"/OSX"
	#elif	defined(PLATFORM_WIN)
						"/Win"
	#elif	defined(PLATFORM_LINUX)
						"/Linux"
	#else
						"/#unk"
	#endif
	;
}

/////////////////////////////////////////////////////////////////
// Platform independent implementations
bool os::GetSystemMemoryInfo(ULONGLONG* free, ULONGLONG* total)
{
	if(free)*free = 0;
	if(total)*total = 0;	
	
#if defined(PLATFORM_WIN)
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof(ms);
	if(GlobalMemoryStatusEx(&ms))
	{
		if(free)*free = ms.ullAvailPhys;
		if(total)*total = ms.ullTotalPhys;
		return true;
	}
#elif defined(PLATFORM_IOS) || defined(PLATFORM_MAC)
	mach_port_t host_port;
	mach_msg_type_number_t host_size;
	vm_size_t pagesize;

    host_port = mach_host_self();
    host_size = sizeof(vm_statistics_data_t) / sizeof(integer_t);
    host_page_size(host_port, &pagesize);        

    vm_statistics_data_t vm_stat;
    if(host_statistics(host_port, HOST_VM_INFO, (host_info_t)&vm_stat, &host_size) == KERN_SUCCESS)
	{
		if(free)*free = (vm_stat.inactive_count + vm_stat.free_count) * pagesize;
		if(total)*total = (vm_stat.inactive_count + vm_stat.free_count + vm_stat.active_count + vm_stat.wire_count) * pagesize;
		return true;
	}
#elif defined(PLATFORM_ANDROID) || defined(PLATFORM_LINUX)
	rt::String meminfo;
	if(os::File::LoadText("/proc/meminfo", meminfo))
	{
		rt::String_Ref line;
		while(meminfo.GetNextLine(line))
		{
			if(total && line.StartsWith("MemTotal:"))
			{	line.TrimLeft(9).ToNumber(*total);
				*total *= 1024;
			}
			if(free && line.StartsWith("MemAvailable:"))
			{	line.TrimLeft(13).ToNumber(*free);
				*free *= 1024;
			}
		}
	}
#else
	#error TBD
#endif
	return false;
}

bool os::GetProcessMemoryLoad(SIZE_T* vmem, SIZE_T* phy_mem)
{	// http://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process
#if defined(PLATFORM_WIN)
	PROCESS_MEMORY_COUNTERS_EX pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	if(vmem)*vmem = pmc.PrivateUsage;
	if(phy_mem)*phy_mem = pmc.WorkingSetSize;
	return true;
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDRIOD)
	struct _call
	{	static int parseLine(char* line){
			// This assumes that a digit will be found and the line ends in " Kb".
			int i = strlen(line);
			const char* p = line;
			while (*p <'0' || *p > '9') p++;
			line[i-3] = '\0';
			i = atoi(p);
			return i;
		}
	};
	
	if(phy_mem)*phy_mem = 0;
	if(vmem)*vmem = 0;
	FILE* file = fopen("/proc/self/status", "r");
	if(file)
	{
		int result = -1;
		char line[128];

		while (fgets(line, 128, file) != NULL){
			if (phy_mem && strncmp(line, "VmRSS:", 6) == 0){
				*phy_mem = _call::parseLine(line);
				break;
			}
			if (vmem && strncmp(line, "VmSize:", 7) == 0){
				*vmem = _call::parseLine(line);
				break;
			}
		}
		fclose(file);
	}else return false;
#elif defined(PLATFORM_MAC) || defined(PLATFORM_IOS)
	struct task_basic_info t_info;
	mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

	if (KERN_SUCCESS != task_info(mach_task_self(),
								  TASK_BASIC_INFO, (task_info_t)&t_info, 
								  &t_info_count))
	{
		return false;
	}

	if(phy_mem)*phy_mem = t_info.resident_size;
	if(vmem)*vmem = t_info.virtual_size;
#else
	#error TBD
#endif

	return true;
}

UINT os::GetNumberOfProcessors()
{
#if defined(PLATFORM_WIN)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
#elif defined(PLATFORM_IOS) || defined(PLATFORM_MAC)
	unsigned numCPUs;
	int mib[2U] = { CTL_HW, HW_NCPU };
    size_t sizeOfNumCPUs = sizeof(numCPUs);
    int status = sysctl(mib, 2U, &numCPUs, &sizeOfNumCPUs, NULL, 0U);
    if(status)
        return 1;
	else
		return numCPUs;
#else
	return (UINT)sysconf (_SC_NPROCESSORS_ONLN);
#endif
}

UINT os::GetNumberOfPhysicalProcessors()
{
	unsigned regs[4];
	CPUID(1, regs);
	unsigned cpuFeatures = regs[3]; // EDX
	UINT num = GetNumberOfProcessors();
	if(num > 1 && ((num&1) == 0) && (cpuFeatures&(1 << 28)))  // HTT
		return num/2;
	else
		return num;
}

void os::CPUID(unsigned i, unsigned regs[4])
{
#if defined(PLATFORM_WIN)
  __cpuid((int *)regs, (int)i);
#elif defined(PLATFORM_MAC)
  asm volatile
    ("cpuid" : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3])
     : "a" (i), "c" (0));
  // ECX is set to zero for CPUID function 4
#else
	rt::Zero(regs);
#endif
}

void os::GetOSVersion(rt::String& name, bool rich_info)
{
#if defined(PLATFORM_WIN)
	OSVERSIONINFOW vi;
	vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
	GetVersionExW(&vi);
	if(rich_info)
	{
		name = rt::SS("win") + vi.dwMajorVersion + '.' + vi.dwMinorVersion + '.' + vi.dwBuildNumber;
#ifdef PLATFORM_64BIT
		name += rt::SS("-x64");
#else
		BOOL is64 = FALSE;
		IsWow64Process(GetCurrentProcess(), &is64);
		if(is64)
			name += rt::SS("-x64");
		else
			name += rt::SS("-x86");
#endif
	}
	else
	{	name = rt::String_Ref() + vi.dwMajorVersion + '.' + vi.dwMinorVersion;
	}
#elif defined(PLATFORM_IOS) 
	// [[NSProcessInfo processInfo] operatingSystemVersion] in iOS 8 and above.
#elif defined(PLATFORM_MAC)
	SInt32 major, minor, bugfix;
	Gestalt(gestaltSystemVersionMajor, &major);
	Gestalt(gestaltSystemVersionMinor, &minor);
	if(rich_info)
	{
		Gestalt(gestaltSystemVersionBugFix, &bugfix);
		name = rt::SS("darwin") + major + '.' + minor + '.' + bugfix;
	}
	else
	{	name = rt::String_Ref() + major + '.' + minor;
	}
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDRIOD)
	struct utsname out;
	if(uname(&out))
	{
		name = os::GetBuildSpecificationString();
	}
	else
	{
		if(rich_info)
		{	name = rt::String_Ref(out.nodename) + " (" + out.release + ") " + out.machine;
		}
		else
		{	name = out.release;
		}
	}
#else
	#error TBD
#endif
}

int os::GetDimensionOfScreens(rt::Vec2i* pDim, UINT dim_size) // return # of screens
{
#if defined(PLATFORM_WIN)
	struct _cb_struct
	{	
		rt::Vec2i*	pDim;
		UINT		dim_size;
		UINT		count;

		static BOOL CALLBACK MonitorEnumProc(
			HMONITOR hMonitor,
			HDC      hdcMonitor,
			LPRECT   lprcMonitor,
			LPARAM   dwData
		)
		{	_cb_struct& t = *((_cb_struct*)dwData);
			if(t.count < t.dim_size)
			{
				t.pDim[t.count].width = lprcMonitor->right - lprcMonitor->left;
				t.pDim[t.count].height = lprcMonitor->bottom - lprcMonitor->top;
				t.count++;
				return true;
			}
			else return false;
		}
	};
	_cb_struct cbs;
	cbs.count = 0;
	cbs.dim_size = dim_size;
	cbs.pDim = pDim;
		
	EnumDisplayMonitors(NULL, NULL, _cb_struct::MonitorEnumProc, (LPARAM)&cbs);
	return cbs.count;
#elif defined(PLATFORM_IOS)
	ASSERT(0);
#elif defined(PLATFORM_MAC)
    return _objc_get_screens_dim(pDim, dim_size);
#elif defined(PLATFORM_LINUX)
	Display* d = XOpenDisplay(NULL);
	dim_size = rt::min(XScreenCount(d), (int)dim_size);
	UINT i = 0;
	for(; i<dim_size; i++)
	{
		Screen* s = XScreenOfDisplay(d, i);
		if(s)
		{
			pDim[i].x = s->width;
			pDim[i].y = s->height;
		}
	}
	return i;
#else
	#error TBD
#endif
    return 0;
}

int os::GetLastError()
{
#if		defined(PLATFORM_WIN)
	return ::GetLastError();
#else
	return errno;
#endif
}

void os::GetHostName(rt::String& name)
{
#if defined(PLATFORM_WIN)
	WCHAR buf[1024] = {L'\x0'};
	DWORD len = sizeofArray(buf);
	GetComputerNameW(buf, &len);
	name = __UTF8(buf);
#else
	char buf[1024] = {'\x0'};
	gethostname(buf, sizeof(buf));
	name = buf;
#endif
}

void os::GetLogonUserName(rt::String& name)
{
#if defined(PLATFORM_WIN)
	WCHAR buf[1024] = {L'\x0'};
	DWORD len = sizeofArray(buf);
	::GetUserNameW(buf, &len);
	name = __UTF8(buf);
#elif defined(PLATFORM_ANDROID)
	ASSERT(0);
#else
	char buf[1024] = {'\x0'};
	getlogin_r(buf, sizeof(buf));
	name = buf;
#endif
}

void os::SetAppTitle(LPCSTR title)
{
#if defined(PLATFORM_WIN)
	if(GetConsoleWindow())
	{	SetConsoleTitleW(os::__UTF16(title));
	}
	else
	{	ASSERT(0);
	}
#else
	ASSERT(0);
#endif
}

void os::GetExecutableFilename(rt::String& name)
{
#if defined(PLATFORM_WIN)
	WCHAR fn[MAX_PATH];
	::GetModuleFileNameW(NULL, fn, sizeofArray(fn));
	name = os::__UTF8(fn);
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDRIOD)
	char exepath[2048];
	int len = readlink("/proc/self/exe", exepath, sizeof(exepath)-1);
	if(len > 0)name = rt::String_Ref(exepath, len);
	else name.Empty();
#elif defined(PLATFORM_MAC) || defined(PLATFORM_IOS)
	char path[1024];
	int path_len = _objc_get_bundle_path(path, 1024);
	name = rt::String_Ref(path, path_len);
#else
	ASSERT(0);
#endif
}

bool os::GetProcessorTimes(ULONGLONG* pbusy, ULONGLONG* ptotal)
{
#if defined(PLATFORM_WIN)
	FILETIME f[3];
	if(GetSystemTimes(&f[0],&f[1],&f[2]))
	{	int div = os::GetNumberOfProcessors()*10000;
		if(pbusy)*pbusy = (((ULONGLONG&)f[1]) + ((ULONGLONG&)f[2]) - (ULONGLONG&)f[0])/div;
		if(ptotal)*ptotal = (((ULONGLONG&)f[1]) + ((ULONGLONG&)f[2]))/div;
		return true;
	}
#elif defined(PLATFORM_IOS) || defined(PLATFORM_MAC)
	natural_t numCPUs = 0U;
	processor_info_array_t cpuInfo;
	mach_msg_type_number_t numCpuInfo;
	
	if(KERN_SUCCESS == host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numCPUs, &cpuInfo, &numCpuInfo))
	{
		ULONGLONG inidle = 0, total = 0;
		for(unsigned i = 0U; i < numCPUs; ++i) 
		{
			total +=cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] + 
					cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] + 
					cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE] + 
					cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE];
			inidle += cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE];
		}
		if(pbusy)*pbusy = 10*(total - inidle)/numCPUs;
		if(ptotal)*ptotal = 10*total/numCPUs;
		return true;
	}
#elif defined(PLATFORM_ANDROID) || defined(PLATFORM_LINUX)
	rt::String sss;
	if(os::File::LoadText("/proc/stat", sss))
	{
		rt::String_Ref line;
		while(sss.GetNextLine(line))
		{	
			if(line.StartsWith("cpu  "))
			{
				rt::String_Ref nums[100];
				int co = line.TrimLeft(5).Split(nums, 100, ' ');
				if(co <= 3)return false;
				ULONGLONG busy = 0;
				ULONGLONG total = 0;
				for(int i=0;i<co;i++)
				{	UINT v;
					nums[i].ToNumber(v);
					total += v;
				}
				for(int i=0;i<3;i++)
				{	UINT v;
					nums[i].ToNumber(v);
					busy += v;
				}
				busy = busy*1000/HZ;
				total = total*1000/HZ;
				int numCPUs = os::GetNumberOfProcessors();
				if(pbusy)*pbusy = busy/numCPUs;
				if(ptotal)*ptotal = total/numCPUs;
				return true;
			}
		}
	}
#else
	ASSERT_STATIC(0);
#endif
	return false;
}

UINT os::GetPowerState()		// precentage of battery remaining
{
#if defined(PLATFORM_WIN)
	SYSTEM_POWER_STATUS ps;
	if(::GetSystemPowerStatus(&ps))
	{	if(ps.ACLineStatus == 1)return 100;
		if(ps.ACLineStatus == 0)
			return rt::min<int>(100,ps.BatteryLifePercent);
	}
	return 100;
#elif defined(PLATFORM_MAC)
	///////////////////////////////////////////////////////////////////
	// IOKit.framework is required, add it to the project
	// How? check this video: http://www.youtube.com/watch?v=La58YR9hTNY
	CFTimeInterval tiv = IOPSGetTimeRemainingEstimate();
	if(tiv == kIOPSTimeRemainingUnlimited || tiv == kIOPSTimeRemainingUnknown)return 100;
	return rt::min(2*3600.0, tiv)*100/(2*3600);
#elif defined(PLATFORM_IOS)
	return _objc_get_battery_state();
#elif defined(PLATFORM_ANDROID)
	return 100; // not supported for Android
#elif defined(PLATFORM_LINUX)
	//  /sys/class/power_supply/BAT0...
	//  /proc/acpi/battery/BAT1/state
	return 100; // not supported for Android
#else
	#error TBD
#endif
}


namespace os
{

namespace _details
{
	INLFUNC int		SolveMonthLetter(int l)
	{	if(l > 'Z')return l - ('a' - 'A');
		return l;
	}
	int		SolveMonth(const rt::String_Ref& seg)
	{	if(seg.GetLength() < 3)return 0;
		int sum = SolveMonthLetter(seg[0])*10000 + SolveMonthLetter(seg[1])*100 + SolveMonthLetter(seg[2]);
		switch(sum)
		{
			case 'j'*10000 + 'a'*100 + 'n': return 1;
			case 'f'*10000 + 'e'*100 + 'b': return 2;
			case 'm'*10000 + 'a'*100 + 'r': return 3;
			case 'a'*10000 + 'p'*100 + 'r': return 4;
			case 'm'*10000 + 'a'*100 + 'y': return 5;
			case 'j'*10000 + 'u'*100 + 'n': return 6;
			case 'j'*10000 + 'u'*100 + 'l': return 7;
			case 'a'*10000 + 'u'*100 + 'g': return 8;
			case 's'*10000 + 'e'*100 + 'p': return 9;
			case 'o'*10000 + 'c'*100 + 't': return 10;
			case 'n'*10000 + 'o'*100 + 'v': return 11;
			case 'd'*10000 + 'e'*100 + 'c': return 12;
			default: return 0;
		}
	}
	bool	SolveDate(const rt::String_Ref seg[3], const int num[3], int& year, int& month, int& day)
	{
		if(num[0] == 0 && (month = SolveMonth(seg[0]))>0){ year = num[2]; day = num[1]; } else // March 20,2015
		if(num[1] == 0 && (month = SolveMonth(seg[1]))>0){ year = num[2]; day = num[0]; if(year<day)rt::Swap(year,day); } else // 2015 March 10 or 10 March 2015
		if(num[2] == 0 && (month = SolveMonth(seg[2]))>0){ year = num[0]; day = num[1]; } else // 2015, 10 March
		if(num[2] == 0 || num[1] == 0 || num[0] == 0){ return false; } else
		if(num[0] > 1000){ year = num[0]; month = num[1]; day = num[2]; } else // 2015, 3, 20
		if(num[2] > 1000){ month = num[0]; day = num[1]; year = num[2]; } else
			return false;

		return month >=1 && month <=12 && day>=1 && day<=31;
	}
};

int Timestamp::Fields::FromString(const rt::String_Ref& s, bool low_bound)
{
	static const rt::CharacterSet seps(rt::SS(" -/:.+,;\t\r\nTZ"));
	static const rt::SS pm("pm");
	static const rt::SS PM("PM");

	DayOfWeek = 0;
	rt::String_Ref	f[7];
	int				fi[7];
	UINT co = s.Split<true>(f,7,seps);
	for(UINT i=0;i<co;i++)
		if(f[i].ToNumber(fi[i])==0)return -100;
	switch(co)
	{
	case 7:	// yyyy mm dd hh min sec msec
		Hour = fi[3];	Minute = fi[4];	Second = fi[5];	MillSecond = fi[6];
		if(_details::SolveDate(f, fi, Year, Month, Day))
			return 7;
	case 6:	// yyyy mm dd hh min sec
		Year = fi[0];	Month = fi[1];	Day = fi[2];
		Hour = fi[3];	Minute = fi[4];	Second = fi[5];
		MillSecond = low_bound?0:999;
		if(_details::SolveDate(f, fi, Year, Month, Day))
			return 6;
	case 5: // yyyy mm dd hh min
		Year = fi[0];	Month = fi[1];	Day = fi[2];
		Hour = fi[3];	Minute = fi[4];	
		if(low_bound){ Second = MillSecond = 0; }
		else{ Second = 59;	MillSecond = 999; }
		if(Hour<12 && (f[4].EndsWith(pm) || f[4].EndsWith(PM)))Hour += 12;
		if(_details::SolveDate(f, fi, Year, Month, Day))
			return 5;
	case 4: // hh min sec msec
		Year = 1970;	Month = 1;	Day = 1;
		Hour = fi[0];	Minute = fi[1];	Second = fi[2];	MillSecond = fi[3];
		return 4;
	case 3: // yyyy mm dd, or hh min sec
		if(s.FindCharacter(':')>0)
		{	Year = 1970;	Month = 1;	Day = 1;
			Hour = fi[0];	Minute = fi[1];	Second = fi[2];
			MillSecond = low_bound?0:999;
			return 3;
		}
		else
		{
			if(low_bound){ Hour = Minute = Second = MillSecond = 0; }
			else{ Hour = 23; Minute = 59; Second = 59;	MillSecond = 999; }
			if(_details::SolveDate(f, fi, Year, Month, Day))
				return 3;
		}
	case 2: // hh min
		Year = 1970;	Month = 1;	Day = 1;
		Hour = fi[0];	Minute = fi[1];
		if(low_bound){ Second = MillSecond = 0; }
		else{ Second = 59;	MillSecond = 999; }
		if(Hour<12 && (f[4].EndsWith(pm) || f[4].EndsWith(PM)))Hour += 12;
		return 2;
	case 1:
		{	
			switch(s.GetLength())
			{
			case 8:
				{	UINT t;
					s.ToNumber(t);
					Year = t/10000;	Month = (t%10000)/100;	Day = t%100;
					if(low_bound){ Hour = Minute = Second = MillSecond = 0; }
					else{ Hour = 23; Minute = 59; Second = 59;	MillSecond = 999; }
				}
				return 3;
			case 8 + 6:
				{	UINT t;
					s.SubStr(0, 8).ToNumber(t);
					Year = t/10000;	Month = (t%10000)/100;	Day = t%100;
					s.SubStr(8, 6).ToNumber(t);
					Hour = t/10000; Month = (t%10000)%100; Day = t%100;
					MillSecond = low_bound?0:999;
				}
				return 6;
			case 8 + 4:
				{	UINT t;
					s.SubStr(0, 8).ToNumber(t);
					Year = t/10000;	Month = (t%10000)/100;	Day = t%100;
					s.SubStr(8, 4).ToNumber(t);
					Hour = t/100; Minute = t%100;
					if(low_bound){ Second = MillSecond = 0; }
					else{ Second = 59;	MillSecond = 999; }
				}
				return 5;
			default:
				return 0;
			}
		}
	default: return 0;
	}
}

bool Timestamp::Fields::FromInternetTimeFormat(const rt::String_Ref& s)
{
	if(	s.GetLength() == 29 &&
		*((DWORD*)&s[25]) == 0x544d4720 // " GMT"
	)
	{	LPCSTR b = s._p;
		//Tue, 15 Nov 1994 12:45:26 GMT
		//0123456789012345678901234567890
		//          1         2 
		Month = ParseMonthName(&b[8]);
		Day =		(b[5]-'0')*10 + b[6] - '0';
		Year =		(b[12]-'0')*1000 + (b[13]-'0')*100 + (b[14]-'0')*10 + b[15]-'0';
	
		Hour =		(b[17]-'0')*10 + b[18] - '0';
		Minute =	(b[20]-'0')*10 + b[21] - '0';
		Second =	(b[23]-'0')*10 + b[24] - '0';
	
		MillSecond = DayOfWeek = 0;
		return true;
	}
	return false;
}

UINT Timestamp::Fields::ToInternetTimeFormat(LPSTR buf) const
{
	return (UINT)
	sprintf(buf, "%s, %02d %s %04d %02d:%02d:%02d GMT",
			GetDayOfWeekName().Begin(),		Day,		GetMonthName().Begin(),
			Year,					Hour,		Minute,
			Second);
}


bool Timestamp::GetDateTime(Fields& fields) const
{
	__time64_t t = _Timestamp/1000;
	fields.MillSecond = (int)(_Timestamp%1000);
	
	struct tm * ptm;
#if defined(PLATFORM_WIN)
	ptm = _gmtime64(&t);
#elif defined(PLATFORM_IOS) || defined(PLATFORM_MAC) || defined(PLATFORM_LINUX)
    ptm = gmtime((time_t*)&t);
#else
	ptm = gmtime64(&t);
#endif

	if(ptm)
	{
		fields.Day = ptm->tm_mday;
		fields.DayOfWeek = ptm->tm_wday;
		fields.Hour = ptm->tm_hour;
		fields.Minute = ptm->tm_min;
		fields.Month = ptm->tm_mon+1;
		fields.Second = ptm->tm_sec;
		fields.Year = ptm->tm_year+1900;
		return true;
	}
	return false;
}

bool Timestamp::GetLocalDateTime(Fields& fields) const	// Local Time
{
	__time64_t t = _Timestamp/1000;
	fields.MillSecond = (int)(_Timestamp%1000);
	
	struct tm * ptm;
#if defined(PLATFORM_WIN)
	ptm = _localtime64(&t);
#elif defined(PLATFORM_IOS) || defined(PLATFORM_MAC) || defined(PLATFORM_LINUX)
    ptm = localtime((time_t*)&t);
#else
	ptm = localtime64(&t);
#endif

	if(ptm)
	{
		fields.Day = ptm->tm_mday;
		fields.DayOfWeek = ptm->tm_wday;
		fields.Hour = ptm->tm_hour;
		fields.Minute = ptm->tm_min;
		fields.Month = ptm->tm_mon+1;
		fields.Second = ptm->tm_sec;
		fields.Year = ptm->tm_year+1900;
		return true;
	}
	return false;
}

bool Timestamp::GetDateTime(Fields& f, int Timezone) const	// Local Time
{
	Timestamp t(_Timestamp + Timezone*3600000LL);
	return t.GetDateTime(f);
}

bool Timestamp::SetLocalDateTime(const Fields& f)
{
	struct tm tmstruct = { f.Second, f.Minute, f.Hour, f.Day, f.Month-1, f.Year-1900, 0, 0, 0 };
	__time64_t t;
#if defined(PLATFORM_WIN)
	t = _mktime64(&tmstruct);
#elif defined(PLATFORM_IOS) || defined(PLATFORM_MAC) || defined(PLATFORM_LINUX)
    t = mktime(&tmstruct);
#else
	t = mktime64(&tmstruct);
#endif
	if(t == -1)return false;
	_Timestamp = t*1000 + f.MillSecond;
	return true;
}

bool Timestamp::SetDateTime(const Fields& f, int Timezone)
{
	Timestamp t;
	if(t.SetDateTime(f))
	{	_Timestamp = t._Timestamp - Timezone*3600000LL;
		return true;
	}
	else return false;
}


bool Timestamp::SetDateTime(const Fields& f)
{
	struct tm tmstruct = {	f.Second, f.Minute, f.Hour, f.Day, f.Month-1, f.Year-1900, 0, 0, 0 };
	__time64_t t;
#if defined(PLATFORM_WIN)
	t = _mkgmtime64(&tmstruct);
#elif defined(PLATFORM_ANDROID)
	t = timegm64(&tmstruct);
#else
	t = timegm(&tmstruct);
#endif
	if(t == -1)return false;
	_Timestamp = t*1000 + f.MillSecond;
	return true;
}

rt::String_Ref Timestamp::GetMonthName(int month)
{	
	ASSERT(month <= 12 && month > 0);
	static const LPCSTR month_name = 
	"Jan\0Feb\0Mar\0Apr\0May\0Jun\0Jul\0Aug\0Sep\0Oct\0Nov\0Dec\0";
	return rt::String_Ref(&month_name[(month-1)*4],3);
}

int	Timestamp::ParseMonthName(LPCSTR b)
{	
	static const int month_hash[21] = { 10, 5, 9, 0, 0, 7, 0, 6, 1, 0, 0, 0, 8, 11, 0, 3, 12, 2, 4, 0, 0 };
	return month_hash[(b[0]+b[1]+b[2])%21];
}

LONGLONG Timestamp::ParseTimeSpan(const rt::String_Ref& x)
{	
	int offset;
	double v;
	if((offset = x.ToNumber(v))>=0)
	{
		rt::String_Ref unit = x.SubStr(offset).TrimLeftSpace();
		switch(unit[0])
		{
		case 's': case 'S':	return (LONGLONG)(v*1000.0 + 0.5);
		case 'm': case 'M':	return (LONGLONG)(v*60000.0 + 0.5);
		case 'h': case 'H':	return (LONGLONG)(v*3600000.0 + 0.5);
		case 'd': case 'D':	return (LONGLONG)(v*3600000.0*24 + 0.5);
		case 'w': case 'W':	return (LONGLONG)(v*3600000.0*24*7 + 0.5);
		default :	return (LONGLONG)(v + 0.5);	// millisecond for default
		}
	}

	return 0;
}


rt::String_Ref Timestamp::GetDayOfWeekName(int weekday)
{	
	ASSERT(weekday < 7 && weekday >= 0);
	static const LPCSTR day_name = "Sun\0Mon\0Tue\0Wed\0Thu\0Fri\0Sat\0";
	return rt::String_Ref(&day_name[weekday*4],3);
}

int Timestamp::GetMonthFromDays(int DaysSinceJan1, int year)
{
	static const BYTE month_table[365] = 
	{
		1 ,1 ,1 ,1 ,1 , 1 ,1 ,1 ,1 ,1 , 1 ,1 ,1 ,1 ,1 , 1 ,1 ,1 ,1 ,1 , 1 ,1 ,1 ,1 ,1 , 1 ,1 ,1 ,1 ,1 , 1 ,
		2 ,2 ,2 ,2 ,2 , 2 ,2 ,2 ,2 ,2 , 2 ,2 ,2 ,2 ,2 , 2 ,2 ,2 ,2 ,2 , 2 ,2 ,2 ,2 ,2 , 2 ,2 ,2 ,
		3 ,3 ,3 ,3 ,3 , 3 ,3 ,3 ,3 ,3 , 3 ,3 ,3 ,3 ,3 , 3 ,3 ,3 ,3 ,3 , 3 ,3 ,3 ,3 ,3 , 3 ,3 ,3 ,3 ,3 , 3 ,
		4 ,4 ,4 ,4 ,4 , 4 ,4 ,4 ,4 ,4 , 4 ,4 ,4 ,4 ,4 , 4 ,4 ,4 ,4 ,4 , 4 ,4 ,4 ,4 ,4 , 4 ,4 ,4 ,4 ,4 ,
		5 ,5 ,5 ,5 ,5 , 5 ,5 ,5 ,5 ,5 , 5 ,5 ,5 ,5 ,5 , 5 ,5 ,5 ,5 ,5 , 5 ,5 ,5 ,5 ,5 , 5 ,5 ,5 ,5 ,5 , 5 ,
		6 ,6 ,6 ,6 ,6 , 6 ,6 ,6 ,6 ,6 , 6 ,6 ,6 ,6 ,6 , 6 ,6 ,6 ,6 ,6 , 6 ,6 ,6 ,6 ,6 , 6 ,6 ,6 ,6 ,6 ,
		7 ,7 ,7 ,7 ,7 , 7 ,7 ,7 ,7 ,7 , 7 ,7 ,7 ,7 ,7 , 7 ,7 ,7 ,7 ,7 , 7 ,7 ,7 ,7 ,7 , 7 ,7 ,7 ,7 ,7 , 7 ,
		8 ,8 ,8 ,8 ,8 , 8 ,8 ,8 ,8 ,8 , 8 ,8 ,8 ,8 ,8 , 8 ,8 ,8 ,8 ,8 , 8 ,8 ,8 ,8 ,8 , 8 ,8 ,8 ,8 ,8 , 8 ,
		9 ,9 ,9 ,9 ,9 , 9 ,9 ,9 ,9 ,9 , 9 ,9 ,9 ,9 ,9 , 9 ,9 ,9 ,9 ,9 , 9 ,9 ,9 ,9 ,9 , 9 ,9 ,9 ,9 ,9 ,
		10,10,10,10,10, 10,10,10,10,10, 10,10,10,10,10, 10,10,10,10,10, 10,10,10,10,10, 10,10,10,10,10, 10,
		11,11,11,11,11, 11,11,11,11,11, 11,11,11,11,11, 11,11,11,11,11, 11,11,11,11,11, 11,11,11,11,11,
		12,12,12,12,12, 12,12,12,12,12, 12,12,12,12,12, 12,12,12,12,12, 12,12,12,12,12, 12,12,12,12,12, 12
	};

	if(DaysSinceJan1<= (31+28))return month_table[DaysSinceJan1];
	bool isleapyear = IsLeapYear(year);
	if(!isleapyear)
		return month_table[DaysSinceJan1];
	else
		return month_table[DaysSinceJan1-1];
}

} //  namespace os


namespace os
{
namespace _details
{
	SIZE_T	MEMORY_PAGESIZE = 0;
} // namespace _details

LPVOID VMAlloc(SIZE_T length)
{
	length = (length+_details::MEMORY_PAGESIZE-1)/_details::MEMORY_PAGESIZE*_details::MEMORY_PAGESIZE;

#if defined(PLATFORM_WIN)
	return ::VirtualAlloc(NULL, length, MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
#else
	return ::mmap(NULL, length, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#endif
}

void VMFree(LPVOID ptr, SIZE_T size)
{
	size = (size+_details::MEMORY_PAGESIZE-1)/_details::MEMORY_PAGESIZE*_details::MEMORY_PAGESIZE;

#if defined(PLATFORM_WIN)
	::VirtualFree(ptr, size, MEM_RELEASE);
#else
	::munmap(ptr, size);
#endif
}

bool VMLock(LPVOID ptr, SIZE_T size)		// ensure not page fault
{
#if defined(PLATFORM_WIN)
	return ::VirtualLock(ptr, size);
#else
	return 0 == ::mlock(ptr, size);
#endif
}

bool VMUnlock(LPVOID ptr, SIZE_T size)
{
#if defined(PLATFORM_WIN)
	return ::VirtualUnlock(ptr, size);
#else
	return 0 == ::munlock(ptr, size);
#endif
}


bool CreateGUID(BYTE id[16])
{
#if defined(PLATFORM_WIN)
	RPC_STATUS ret = ::UuidCreate((UUID*)id);
	return ret == RPC_S_OK || ret == RPC_S_UUID_LOCAL_ONLY;
	
#elif defined(PLATFORM_ANDROID) || defined(PLATFORM_LINUX)
	FILE * f = fopen("/proc/sys/kernel/random/uuid","r");
	bool ret;
	if(f)
	{	ret = (16 == fread(id, 1,16, f));
		fclose(f);
		return ret;
	}
	else return false;
#else
	uuid_generate(*((uuid_t*)id));
    return true;
#endif
}

DWORD  GetRandomSeed()
{
	ULONGLONG a;
	os::GetSystemMemoryInfo(&a);
    return TickCount::Get() * (UINT)(a) * (UINT)(a>>32);
}

bool GetDeviceUID(rt::String& str)
{
#if defined(PLATFORM_WIN)
	str = "win";
	HANDLE tk;
	TOKEN_USER* tu = NULL;
	DWORD ret_len = 0;
	LPSTR sid = NULL;
	if(	::OpenProcessToken(::GetCurrentProcess(), TOKEN_READ, &tk) &&
		(::GetTokenInformation(tk, TokenUser, NULL, 0, &ret_len)||true) &&
		(tu = (TOKEN_USER*)_Malloc32AL(BYTE,ret_len)) &&
		::GetTokenInformation(tk, TokenUser, tu, ret_len, &ret_len) &&
		::ConvertSidToStringSidA(tu->User.Sid, &sid) &&
		sid
	)
	{	rt::String s = sid+3;
		s.Replace("-","");
		str += rt::SS() + '-' + s.TrimLeft(2);
		LocalFree(sid);
	}
	_SafeFree32AL(tu);

	WCHAR txt[1024];
	if(LoadRegKeyString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardProduct", txt, sizeofArray(txt)))
	{	str += rt::SS() + '-' + os::__UTF8(txt);
	}

	{	HKEY key = 0;
		DWORD type = REG_SZ;
		DWORD bufsize = sizeofArray(txt);
		LPCWSTR kp = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
		if(	(	(ERROR_SUCCESS == ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kp, 0, KEY_READ|KEY_WOW64_64KEY, &key)) ||
				(ERROR_SUCCESS == ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kp, 0, KEY_READ, &key)) 
			) &&
			(ERROR_SUCCESS == ::RegQueryValueExW(key,L"ProductId", NULL, &type, (LPBYTE)txt, &bufsize))
		)
		{	str += rt::SS() + '-' + os::__UTF8(txt); 
		}
	
		if(key)::RegCloseKey(key);
	}

	DWORD vol = 0;
	if(::GetVolumeInformationA(::getenv("SystemDrive"), NULL, 0, &vol, NULL, NULL, NULL, 0))
	{	str += rt::SS() + '-' + rt::tos::Binary<>(vol);
	}

	return str.GetLength()>3;
#elif defined(PLATFORM_MAC)
	bool ret = false;
    io_registry_entry_t ioRegistryRoot = NULL;
    CFStringRef uuidCf = NULL;
	char uid_string[64];
	if( (ioRegistryRoot = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/")) &&
		(uuidCf = (CFStringRef) IORegistryEntryCreateCFProperty(ioRegistryRoot, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0)) &&
		CFStringGetCString(uuidCf, uid_string, 64, kCFStringEncodingMacRoman) &&
		rt::String_Ref(uid_string).RemoveCharacters('-').GetLength() == 32
	)
	{	str = rt::SS("mac-") + uid_string;
		ret = true;
	}
    if(uuidCf)CFRelease(uuidCf);
	if(ioRegistryRoot)IOObjectRelease(ioRegistryRoot);
	return ret;
#elif defined(PLATFORM_IOS)
	char uid_string[64];
	if(_objc_get_device_uid(uid_string) &&
	   rt::String_Ref(uid_string).TrimCharacters('-').GetLength() == 32
	)
	{	str = rt::SS("ios-") + uid_string;
		return true;
	}
	else return false;
#elif defined(PLATFORM_LINUX)
	rt::String board_serial, product_uuid;
	if(	os::File::LoadText("/sys/class/dmi/id/board_serial", board_serial) ||
		os::File::LoadText("/sys/class/dmi/id/product_uuid", product_uuid)
	)
	{	str = rt::SS("linux-") + board_serial + '-' + product_uuid;	
		return true;
	}
	else return false;
#elif defined(PLATFORM_ANDROID)
	//import android.provider.Settings.Secure;
	// private String android_id = Secure.getString(getContext().getContentResolver(), Secure.ANDROID_ID);
#else
#endif
	return false;
}


void BruteforceExit()
{
#if defined(PLATFORM_WIN)
	::TerminateProcess(::GetCurrentProcess(), 0);
#else
	_exit(0);
#endif

}


HANDLE LoadDynamicLibrary(LPCSTR fn)
{
#if defined(PLATFORM_WIN)
	return (HANDLE)::LoadLibraryW(os::__UTF16(fn));
#else
	return (HANDLE)::dlopen(fn, RTLD_LOCAL|RTLD_NOW);
#endif
}

LPVOID GetDynamicLibrarySymbol(HANDLE dll, LPCSTR fn)
{
#if defined(PLATFORM_WIN)
	return ::GetProcAddress((HMODULE)dll, fn);
#else
	return ::dlsym((void*)dll, fn);
#endif
}

void UnloadDynamicLibrary(HANDLE h)
{
#if defined(PLATFORM_WIN)
	::FreeLibrary((HMODULE)h);
#else
	::dlclose((void *)h);
#endif
}

void SetProcessPriority(int prio)
{
#if defined(PLATFORM_WIN)
	switch(prio)
	{
	case PROCPRIO_REALTIME: ::SetPriorityClass(::GetCurrentProcess(), REALTIME_PRIORITY_CLASS); break;
	case PROCPRIO_HIGH: ::SetPriorityClass(::GetCurrentProcess(), HIGH_PRIORITY_CLASS); break;
	case PROCPRIO_NORMAL: ::SetPriorityClass(::GetCurrentProcess(), NORMAL_PRIORITY_CLASS); break;
	case PROCPRIO_LOW: ::SetPriorityClass(::GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS); break;
	case PROCPRIO_IDLE: ::SetPriorityClass(::GetCurrentProcess(), IDLE_PRIORITY_CLASS); break;
	default: ASSERT(0);
	}
#elif defined(PLATFORM_MAC)
	switch(prio)
	{
	case PROCPRIO_REALTIME: ::setpriority(PRIO_PROCESS, 0, 20); break;
	case PROCPRIO_HIGH: ::setpriority(PRIO_PROCESS, 0, 5); break;
	case PROCPRIO_NORMAL: ::setpriority(PRIO_PROCESS, 0, 0); break;
	case PROCPRIO_LOW: ::setpriority(PRIO_PROCESS, 0, -5); break;
	case PROCPRIO_IDLE: ::setpriority(PRIO_PROCESS, 0, -20); break;
	default: ASSERT(0);
	}
#else
	ASSERT(0);
#endif
}


int  SearchProcess(LPCSTR base_name_in, int* pProcessIds, UINT ProcessIdSize, bool substr_match)	// 0 for not found
{
	rt::String base_name(base_name_in);
	base_name.MakeLower();
	int bufused = 0;

#if defined(PLATFORM_WIN)
	DWORD _process[10240];
	DWORD _proc_byte = 0;
	if(::EnumProcesses(_process, sizeof(_process), &_proc_byte))
	{
		for(int i=0; i<(int)(_proc_byte/sizeof(DWORD)); i++)
		{
			HANDLE h = NULL;
			WCHAR basename[1024];
			if(	(h = ::OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, false, _process[i])) &&
				::GetModuleBaseNameW(h, NULL, basename, sizeofArray(basename))
			)
			{	os::__UTF8 _str(basename);
				_str.MakeLower();

				if(	(substr_match && strstr(_str.Begin(), base_name)) ||
					_str == base_name
				)
				{	pProcessIds[bufused++] = _process[i];
					if(bufused == ProcessIdSize)break;
				}
			}

			if(h)::CloseHandle(h);
		}
	}
#elif defined(PLATFORM_MAC)
#else
#endif

	return bufused;
}

int  SearchProcess(LPCSTR base_name_in, bool substr_match)
{
	int pid = -1;
	if(SearchProcess(base_name_in, &pid, 1, substr_match))
		return pid;
	else
		return -1;
}

bool TerminateProcess(int process_id)
{
#if defined(PLATFORM_WIN)
	HANDLE proc = ::OpenProcess(PROCESS_TERMINATE, false, process_id);
	if(proc)
	{
		return ::TerminateProcess(proc, -1);
	}
	return true;
#elif defined(PLATFORM_ANDROID)
    //kill(process_id, SIGKILL);
    return true;
#else
    ASSERT(0);
	return false;
#endif
}

int  GetProcessId()
{
#if defined(PLATFORM_WIN)
	return ::GetCurrentProcessId();
#else
	return getpid();
#endif
}

} // namespace os


void os::Halt()
{
	_LOG_WARNING("Halt ...");
	for(;;)
		os::Sleep(100);
}

bool os::EncryptData(rt::String& cipertext, const rt::String_Ref& plaintext)
{
#if defined(PLATFORM_WIN)
    if(plaintext.IsEmpty())
    {
        cipertext.Empty();
        return true;
    }
    
    DATA_BLOB	d, out;
    d.pbData = (LPBYTE)plaintext.Begin();
    d.cbData = (DWORD)plaintext.GetLength();
    
    if(CryptProtectData(&d, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out) &&
       out.cbData > 0 &&
       out.pbData
       )
    {
        cipertext.SetLength(out.cbData);
        memcpy(cipertext.Begin(), out.pbData, out.cbData);
        
        rt::Zero(out.pbData, out.cbData);
        LocalFree(out.pbData);
        
        return true;
    }
#else  
    ASSERT(0);
#endif
    
    return false;
}

bool os::DecryptData(rt::String& plaintext, const rt::String_Ref& cipertext)
{
#if defined(PLATFORM_WIN)
    if(cipertext.IsEmpty())
    {
        plaintext.Empty();
        return true;
    }
    
    DATA_BLOB	d, out;
    d.pbData = (LPBYTE)cipertext.Begin();
    d.cbData = (DWORD)cipertext.GetLength();
    
    if(	CryptUnprotectData(&d, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out) &&
       out.cbData > 0 &&
       out.pbData
       )
    {
        plaintext.SetLength(out.cbData);
        memcpy(plaintext.Begin(), out.pbData, out.cbData);
        
        rt::Zero(out.pbData, out.cbData);
        LocalFree(out.pbData);
        
        return true;
    }
#else
    ASSERT(0);
#endif
    
    return false;
}

#if defined(PLATFORM_WIN)
//////////////////////////////////////////////////////////
// All Windows implementations
#include <windows.h>

void os::Sleep(DWORD msec)
{
	::Sleep(msec);
}

bool os::CreateRegKey(HKEY root, LPCWSTR regkey)
{
	HKEY r;
	bool ret = (ERROR_SUCCESS == ::RegCreateKeyW(root, regkey, &r));
	if(ret)::RegCloseKey(r);
	return ret;
}

bool os::LoadRegKeyString(HKEY root, LPCWSTR regkey, LPCWSTR value, LPWSTR textbuf, DWORD bufsize)
{
	HKEY key = NULL;
	DWORD type = REG_SZ;
	bool ret =
		(ERROR_SUCCESS == ::RegOpenKeyExW(root, regkey, 0, KEY_READ, &key)) &&
		(ERROR_SUCCESS == ::RegQueryValueExW(key, value, NULL, &type, (LPBYTE)textbuf, &bufsize));
	
	if(key)::RegCloseKey(key);
	if(!ret)
	{	textbuf[0] = 0;
	}

	return ret;
}

bool os::LoadRegKeyPath(HKEY root, LPCWSTR regkey, LPCWSTR value_name, LPWSTR textbuf, DWORD bufsize, bool resolve_env)
{
	if(LoadRegKeyString(root, regkey, value_name, textbuf, bufsize))
	{
		int len = (int)wcslen(textbuf);
		if(textbuf[len-1] == L'\\' || textbuf[len-1] == _T('/'))
			textbuf[len-1] = 0;

		WCHAR textenv[1024];
		LPWSTR pe, pet;
		if(	resolve_env && 
			(pe = wcschr(textbuf, L'%')) &&
			(pet = wcschr(pe+1, L'%'))
		)
		{	*pet = L'\0';
			int envlen = ::GetEnvironmentVariableW(pe+1, textenv, sizeofArray(textenv));
			memmove(pe + envlen, pet+1, (len - (pet - textbuf))*sizeof(TCHAR));
			memcpy(pe, textenv, envlen*sizeof(TCHAR));
		}

		return true;
	}

	return false;
}

INT os::LoadRegKeyInt(HKEY root, LPCWSTR regkey, LPCWSTR value, INT default_val)
{
	HKEY key = NULL;
	DWORD type = REG_DWORD;
	DWORD len = sizeof(DWORD);
	bool ret =
		(ERROR_SUCCESS == ::RegOpenKeyExW(root, regkey, 0, KEY_READ, &key)) &&
		(ERROR_SUCCESS == ::RegQueryValueExW(key, value, NULL, &type, (LPBYTE)&default_val, &len));
	
	if(key)::RegCloseKey(key);
	return default_val;
}

bool os::SaveRegKeyString(HKEY root, LPCWSTR regkey, LPCWSTR value_name, LPCWSTR string)
{
	HKEY key = NULL;
	bool ret =
		(ERROR_SUCCESS == ::RegOpenKeyExW(root, regkey, 0, KEY_WRITE, &key)) &&
		(ERROR_SUCCESS == ::RegSetValueExW(key, value_name, 0, REG_SZ, (LPCBYTE)string, sizeof(TCHAR)*(DWORD)(1+wcslen(string))));
	
	if(key)::RegCloseKey(key);
	return ret;
}

bool os::SaveRegKeyInt(HKEY root, LPCWSTR regkey, LPCWSTR value_name, INT val)
{
	HKEY key = NULL;
	bool ret =
		(ERROR_SUCCESS == ::RegOpenKeyExW(root, regkey, 0, KEY_WRITE, &key)) &&
		(ERROR_SUCCESS == ::RegSetValueExW(key, value_name, 0, REG_DWORD, (LPCBYTE)&val, 4));
	
	if(key)::RegCloseKey(key);
	return ret;
}

void os::DeleteRegKeyValue(HKEY root, LPCWSTR regkey, LPCWSTR value_name)
{
	HKEY key = NULL;
	bool ret =
		(ERROR_SUCCESS == ::RegOpenKeyExW(root, regkey, 0, KEY_WRITE, &key)) &&
		(ERROR_SUCCESS == ::RegDeleteValueW(key, value_name));
	
	if(key)::RegCloseKey(key);
}

bool os::OpenDefaultBrowser(LPCSTR url, DWORD show_window)
{
	WCHAR file[MAX_PATH+MAX_PATH];
	if(LoadRegKeyString(HKEY_CLASSES_ROOT, L"HTTP\\shell\\open\\command", NULL, file, MAX_PATH+MAX_PATH))
	{
		os::__UTF8 cmdline(file);
		rt::String	str;

		if(cmdline.FindString("%1"))
		{
			str = cmdline;
			str.Replace("%1", url);
		}
		else
		{	str = cmdline + ' ' + url;
		}


		STARTUPINFOW si;
		ZeroMemory(&si,sizeof(si));
		si.cb = sizeof(si);
		si.wShowWindow = (WORD)show_window;

		PROCESS_INFORMATION pi;
		if(::CreateProcessW(NULL,os::__UTF16(str).Begin(),NULL,NULL,false,0,NULL,NULL,&si,&pi))
		{
			::CloseHandle(pi.hProcess);
			::CloseHandle(pi.hThread);
			return true;
		}
	}

	struct _open_url: public os::Thread
	{
		os::__UTF16		url;
		DWORD			show_window;
		static DWORD _call(LPVOID p)
		{	
			::ShellExecuteW(NULL,L"open",((_open_url*)p)->url,NULL,NULL,((_open_url*)p)->show_window);
			delete ((_open_url*)p);
			return os::Thread::THREAD_OBJECT_DELETED_ON_RETURN;
		};
	};

	_open_url* p = new _open_url;
	p->url = url;
	p->show_window = show_window;
	p->Create(_open_url::_call, p);

	return true;
}

#else
//////////////////////////////////////////////////////////
// All linux/BSD implementations
#include <unistd.h>

#if defined(PLATFORM_MAC)
#include <CoreFoundation/CFBundle.h>
#include <ApplicationServices/ApplicationServices.h>

bool os::OpenDefaultBrowser(LPCSTR url_in, DWORD show_window)
{
    CFURLRef url = CFURLCreateWithBytes (
                                         NULL,                  // allocator
                                         (UInt8*)url_in,                   // URLBytes
                                         strlen(url_in),            // length
                                         kCFStringEncodingASCII,      // encoding
                                         NULL                         // baseURL
                                         );
    bool ret = LSOpenCFURLRef(url,0) == noErr;
    CFRelease(url);
    return ret;
}
#else

bool os::OpenDefaultBrowser(LPCSTR url_in, DWORD show_window)
{
	return 0 == system(rt::SS("xdg-open \"") + url_in + '"');
}
#endif

void os::Sleep(DWORD msec)
{
	::usleep(msec*1000);
}

#endif

namespace os
{  
#if defined (PLATFORM_IOS) || defined (PLATFORM_MAC)
struct mach_timebase_info TickCount::__mach_tick_unit;
#endif
} // namespace

namespace os
{

#if		defined(PLATFORM_WIN)
rt::String		_RegAppRegKey;
#elif	defined(PLATFORM_MAC) || defined(PLATFORM_IOS)
#else
#endif

void SetPreferenceLocation(const rt::String_Ref& app_name)
{
#if		defined(PLATFORM_WIN)
	_RegAppRegKey = rt::SS("Software\\") + app_name + rt::SS("\\Preference");
	os::CreateRegKey(HKEY_CURRENT_USER, os::__UTF16(_RegAppRegKey));
#elif	defined(PLATFORM_MAC)
	return; // do nothing on Mac
#else
	ASSERT(0);
#endif
}

INT LoadPreference(const rt::String_Ref& keyname, INT   default_value)
{
#if		defined(PLATFORM_WIN)
	ASSERT(!_RegAppRegKey.IsEmpty());	// call SetPreferenceLocation first
	return LoadRegKeyInt(HKEY_CURRENT_USER, os::__UTF16(_RegAppRegKey), os::__UTF16(keyname), default_value);
#elif	defined(PLATFORM_MAC)
	char out[1024];
	STACK_STRING_BEGIN(sskey) = keyname;
	STACK_STRING_END
	rt::String_Ref ret(out, _objc_preference_load_string(sskey.Begin(), out, sizeof(out)));
	if(!ret.IsEmpty())ret.ToNumber(default_value);
	return default_value;
#else
	ASSERT(0);
	return 0;
#endif
}	 
	 
bool SavePreference(const rt::String_Ref& keyname, INT value)
{	 
#if		defined(PLATFORM_WIN)
	ASSERT(!_RegAppRegKey.IsEmpty());	// call SetPreferenceLocation first
	return SaveRegKeyInt(HKEY_CURRENT_USER, os::__UTF16(_RegAppRegKey), os::__UTF16(keyname), value);
#elif	defined(PLATFORM_MAC)
	STACK_STRING_BEGIN(sskey) = keyname;
	STACK_STRING_END
	_objc_preference_save_string(sskey.Begin(), rt::tos::Number(value).Begin());
	return true;
#else
	ASSERT(0);
	return 0;
#endif
}
	 
bool LoadPreferenceString(const rt::String_Ref& keyname, rt::String& out_val, const rt::String_Ref& default_value)
{	 
#if		defined(PLATFORM_WIN)
	ASSERT(!_RegAppRegKey.IsEmpty());	// call SetPreferenceLocation first
	WCHAR buf[1024];
	if(LoadRegKeyString(HKEY_CURRENT_USER, os::__UTF16(_RegAppRegKey), os::__UTF16(keyname), buf, sizeofArray(buf)))
	{	out_val = os::__UTF8(buf);
		return true;
	}
	out_val = default_value;
	return false;
#elif	defined(PLATFORM_MAC)
	char out[1024];
	STACK_STRING_BEGIN(sskey) = keyname;
	STACK_STRING_END
	rt::String_Ref ret(out, _objc_preference_load_string(sskey.Begin(), out, sizeof(out)));
	if(ret.IsEmpty()){ out_val = default_value; return false; }
	else
	{	out_val = ret;
		return true;
	}
#else
	ASSERT(0);
	return 0;
#endif
}	 
	 
bool SavePreferenceString(const rt::String_Ref& keyname, const rt::String_Ref& value)
{
#if		defined(PLATFORM_WIN)
	ASSERT(!_RegAppRegKey.IsEmpty());	// call SetPreferenceLocation first
	return SaveRegKeyString(HKEY_CURRENT_USER, os::__UTF16(_RegAppRegKey), os::__UTF16(keyname), os::__UTF16(value));
#elif	defined(PLATFORM_MAC)
	STACK_STRING_BEGIN(sskey) = keyname;
	STACK_STRING_END
	STACK_STRING_BEGIN(ssval) = value;
	STACK_STRING_END
	_objc_preference_save_string(sskey.Begin(), ssval.Begin());
	return true;
#else
	ASSERT(0);
	return 0;
#endif
}

} // namespace os


namespace os
{
namespace _details
{
	struct os_class_init
	{
		os_class_init()
		{
		#if defined(PLATFORM_IOS) || defined (PLATFORM_MAC)
            mach_timebase_info(&::os::TickCount::__mach_tick_unit);
            ::os::TickCount::__mach_tick_unit.denom *= 1000000;
		#endif
				
		#if defined(PLATFORM_WIN)
			SYSTEM_INFO si;
			GetSystemInfo(&si);
            ::os::_details::MEMORY_PAGESIZE = rt::max<int>(1024,si.dwPageSize);
		#else
            ::os::_details::MEMORY_PAGESIZE = rt::max<int>(1024,(int)sysconf(_SC_PAGE_SIZE));
		#endif
		}
	};
	os_class_init __init;
}} // namespace os::_details



SIZE_T os::UTF8EncodeLength(LPCU16CHAR pIn, SIZE_T len)	// number of char
{
	SIZE_T outlen = 0;
	for(SIZE_T i=0;i<len;i++)
	{
		WORD c = pIn[i];
		if(c <= 0x7f)
		{	outlen++;	}
		else if(c > 0x7ff)
		{	outlen+=3;	}
		else
		{	outlen+=2;	}
	}
	return outlen;
}

SIZE_T os::UTF8Encode(LPCU16CHAR pIn, SIZE_T len, LPSTR pOut)
{
	LPSTR p = pOut;
	for(SIZE_T i=0;i<len;i++)
	{
		WORD c = pIn[i];
		if(c <= 0x7f)
		{	*p = (char)c;
			p++;
		}
		else if(c > 0x7ff)	// 1110xxxx 	10xxxxxx 	10xxxxxx
		{	*((DWORD*)p) = 0x8080e0 | ((c>>12)&0xf) | ((c<<2)&0x3f00) | ((0x3f&c)<<16);
			p+=3;
		}
		else	// 110xxxxx 	10xxxxxx
		{	*((WORD*)p) = 0x80c0 | ((c>>6)&0x1f) | ((0x3f&c)<<8);
			p+=2;
		}
	}
	return (UINT)(p - pOut);
}

// UTF8 to UTF16
SIZE_T os::UTF8DecodeLength(LPCSTR pIn, SIZE_T len)	// number of wchar
{
	SIZE_T outlen = 0;
	for(SIZE_T i=0;i<len;outlen++)
	{
		BYTE c = pIn[i];
		if(c <= 0x7f)
		{	i++;
		}
		else if((c&0xf0) == 0xe0)	// 1110xxxx 	10xxxxxx 	10xxxxxx
		{	i+=3;
		}
		else if((c&0xe0) == 0xc0)	// 110xxxxx 	10xxxxxx
		{	i+=2;
		}
		else i++;	// skip
	}
	return outlen;
}

SIZE_T UTF8ByteOffset(LPCSTR pIn, SIZE_T len, SIZE_T num_of_utf8_char) // counting number of utf8 chatactors
{
	SIZE_T outlen = 0;
	SIZE_T i=0;
	for(;outlen<num_of_utf8_char;outlen++)
	{
		if(i>=len)return 0;

		BYTE c = pIn[i];
		if(c <= 0x7f)
		{	i++;
		}
		else if((c&0xf0) == 0xe0)	// 1110xxxx 	10xxxxxx 	10xxxxxx
		{	i+=3;
		}
		else if((c&0xe0) == 0xc0)	// 110xxxxx 	10xxxxxx
		{	i+=2;
		}
		else i++;	// skip
	}
	return i;
}

os::U16CHAR	os::UTF8Decode(LPCSTR& p)
{
	BYTE c = p[0];
	if(c <= 0x7f)
	{	
		p++;
		return c;
	}
	
	os::U16CHAR ret;
	if((c&0xf0) == 0xe0)	// 1110xxxx 	10xxxxxx 	10xxxxxx
	{	
		ret = ((c&0xf)<<12) | ((p[1]&0x3f)<<6) | (p[2]&0x3f);
		p+=3;
	}
	else if((c&0xe0) == 0xc0)	// 110xxxxx 	10xxxxxx
	{	
		ret = ((c&0x1f)<<6) | (p[1]&0x3f);
		p+=2;
	}
	else
	{
		p+=4;
		return '?'; // Unicode BMP
	}

	return ret;
}

SIZE_T os::UTF8Decode(LPCSTR pIn, SIZE_T len, LPU16CHAR pOut)		// number of wchar
{
	LPU16CHAR p = pOut;
	for(SIZE_T i=0;i<len;p++)
	{
		BYTE c = pIn[i];
		if(c <= 0x7f)
		{	*p = c;
			i++;
		}
		else if((c&0xf0) == 0xe0)	// 1110xxxx 	10xxxxxx 	10xxxxxx
		{	*p = ((c&0xf)<<12) | ((pIn[i+1]&0x3f)<<6) | (pIn[i+2]&0x3f);
			i+=3;
		}
		else if((c&0xe0) == 0xc0)	// 110xxxxx 	10xxxxxx
		{	*p = ((c&0x1f)<<6) | (pIn[i+1]&0x3f);
			i+=2;
		}
		else i++;
	}
	return (UINT)(p - pOut);
}

SSIZE_T os::UTF8EncodeLengthMax(LPCVOID pIn, SIZE_T len, const rt::String_Ref& charset_name, DWORD* pCharsetIndex)
{
	ASSERT(pCharsetIndex);

	if(charset_name.StartsWith(rt::SS("iso-8859-")))
	{	UINT latin_index;
		charset_name.SubStr(9).ToNumber(latin_index);
		if(latin_index <= 10)
		{
			*pCharsetIndex = CHARSET_LATIN_BASE + latin_index;
			return len*2;
		}
		else
		{
			return -1;
		}
	}
	else if(charset_name == rt::SS("shift-jis"))
	{	*pCharsetIndex = CHARSET_SHIFT_JIS;
		return (len+1)/2*3;
	}
	else if(charset_name.StartsWith(rt::SS("gb")))
	{
		UINT gb_num;
		charset_name.SubStr(2).ToNumber(gb_num);
		switch(gb_num)
		{
		case 2312: *pCharsetIndex = CHARSET_GB_2312; break;
		case 18030:*pCharsetIndex = CHARSET_GB_18030; break;
		default: return -1;
		}
		return (len+1)/2*3; 
	}
	else if(charset_name == rt::SS("big5"))
	{	*pCharsetIndex = CHARSET_BIG5;
		return (len+1)/2*3;
	}
	else if(charset_name.StartsWith("ks") || charset_name == rt::SS("korean"))
	{
		*pCharsetIndex = CHARSET_KOREAN;
		return (len+1)/2*3;
	}
	else if(charset_name == rt::SS("utf-16"))
	{
		*pCharsetIndex = CHARSET_UTF_16;
		return (len+1)/2*3;
	}
	else if(charset_name == rt::SS("utf-8"))
	{
		*pCharsetIndex = CHARSET_UTF_8;
		return len;
	}
	else if(charset_name.StartsWith("koi8-r"))
	{
		*pCharsetIndex = CHARSET_KOI8_R;
		return len*2;
	}
	else if(charset_name.StartsWith("koi8-u"))
	{
		*pCharsetIndex = CHARSET_KOI8_U;
		return len*2;
	}
	else
	{	return -1;
	}
}

SIZE_T os::UTF8Encode(LPCVOID pIn, SIZE_T len, LPSTR pOut, DWORD charset_index)
{
	if(charset_index == CHARSET_UTF_8)
	{	memcpy(pOut, pIn, len);
		pOut[len] = 0;
		return len;
	}
	if(charset_index == CHARSET_UTF_16)
	{	SIZE_T outlen = UTF8Encode((LPCU16CHAR)pIn, len/2, pOut);
		pOut[outlen] = 0;
		return outlen;
	}

#ifdef PLATFORM_WIN
	LPU16CHAR buf;
	rt::Buffer<U16CHAR>	_temp;
	if(len*2 > 64*1024)
	{	// allocate on heap
		_temp.SetSize(len);
		buf = _temp;
	}
	else
	{	// allocate on stack
		buf = (LPU16CHAR)alloca(2*len);
	}
	int outlen = MultiByteToWideChar(charset_index, 0, (LPCCH)pIn, (int)len, buf, (int)len);
	return UTF8Encode(buf, outlen, pOut);
#else
	ASSERT(0);
	return 0;
#endif
}

#ifdef PLATFORM_WIN
namespace os
{

ConsoleProgressIndicator::ConsoleProgressIndicator(ULONGLONG total, LPCSTR hint)
{
	_Total = total;
	_Prog = 0;
	_LastDisplay.LoadCurrentTime();
	_StartTime.LoadCurrentTime();

	if(hint){ _Hint = hint; }
	else{ _Hint = "In Progress: "; }
}

ConsoleProgressIndicator::~ConsoleProgressIndicator()
{
	printf("%s: %llu done, %s                         \n", _Hint.GetString(), _Total, rt::tos::TimeSpan<>(_StartTime.TimeLapse()).Begin());
}

void ConsoleProgressIndicator::_Display()
{
	if(_LastDisplay.TimeLapse() > 330)
	{
		_LastDisplay.LoadCurrentTime();
		if(_Total)
		{
			float prec = _Prog*100.0f/_Total;
			printf("%s: %llu (%0.2f%%), %s remains   \r", _Hint.GetString(), _Prog, prec, rt::tos::TimeSpan<>(_StartTime.TimeLapse()*(_Total-_Prog)/_Prog).Begin());
		}
		else
		{	
			printf("%s: %llu, %s passed   \r", _Hint.GetString(), _Prog, rt::tos::TimeSpan<>(_StartTime.TimeLapse()).Begin());
		}
	}

}

}
#endif


namespace os
{

INLFUNC bool _isb64(int c)
{
	return	(c>='a' && c<='z') ||
			(c>='A' && c<='Z') ||
			(c>='0' && c<='9') ||
			c == '+' ||
			c == '/';
}

};

bool os::Base64Encode(const rt::String_Ref&in, rt::String& out)
{
	if(!out.SetLength(os::Base64EncodeLength(in.GetLength())))
		return false;

	os::Base64Encode(out.Begin(), in.Begin(), in.GetLength());
	return true;
}

bool os::Base64Decode(const rt::String_Ref&in, rt::String& out)
{
	out.SetLength(os::Base64DecodeLength(in.Begin(), in.GetLength()));
	SIZE_T actual_len = out.GetLength();
	bool ret = os::Base64Decode(out.Begin(), &actual_len, in.Begin(), in.GetLength());
	out.SetLength(actual_len);
	return ret;
}

SIZE_T os::Base64EncodeLength(SIZE_T len)
{
	return (len+2)/3*4;
}

void os::Base64Encode(LPSTR pBase64Out,LPCVOID pData, SIZE_T data_len)
{
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	
	LPCBYTE p = (LPCBYTE)pData;
	for(;data_len>=3;data_len-=3,p+=3,pBase64Out+=4)
	{
		pBase64Out[0] = table[p[0]>>2];
		pBase64Out[1] = table[((p[0]&0x3)<<4)|(p[1]>>4)];
		pBase64Out[2] = table[((p[1]&0xf)<<2)|(p[2]>>6)];
		pBase64Out[3] = table[p[2]&0x3f];
	}

	if(data_len == 1)
	{
		pBase64Out[0] = table[p[0]>>2];
		pBase64Out[1] = table[((p[0]&0x3)<<4)];
		pBase64Out[2] = '=';
		pBase64Out[3] = '=';
		//pBase64Out[4] = '\0';
	}
	else if(data_len == 2)
	{
		ASSERT(data_len == 2);
		pBase64Out[0] = table[p[0]>>2];
		pBase64Out[1] = table[((p[0]&0x3)<<4)|(p[1]>>4)];
		pBase64Out[2] = table[((p[1]&0xf)<<2)];
		pBase64Out[3] = '=';
		//pBase64Out[4] = '\0';
	}
	//else
	//	pBase64Out[0] = '\0';
}

SIZE_T os::Base64DecodeLength(LPCSTR pBase64, SIZE_T str_len)
{
	if(str_len<=3)return 0;

	SSIZE_T fulllen = (str_len+3)/4*3;
	SSIZE_T miss = (4 - (str_len&3))&3;

	if(_isb64(pBase64[str_len-2]) && _isb64(pBase64[str_len-1]) && miss <= 2)
	{	
		fulllen -= miss;
	}
	else if(_isb64(pBase64[str_len-2]) && !_isb64(pBase64[str_len-1]))
	{
		fulllen -= (miss + 1);
	}
	else if(!_isb64(pBase64[str_len-2]) && !_isb64(pBase64[str_len-1]) && miss == 0)
	{
		fulllen -= 2;
	}
	else return fulllen;

	return rt::max((SSIZE_T)0, fulllen);
}

bool os::Base64Decode(LPVOID pDataOut, SIZE_T* pDataOutLen,LPCSTR pBase64, SIZE_T str_len)
{
	static const int rev_table[80] = // base on 2B
	{
		62,-1,-1,-1,			//'+' 2B,2C,2D,2E,
		63,						//'/' 2F
		52,53,54,55,56,57,58,59,60,61,	// '0' - '9', 30 - 39
		-1,-1,-1,-1,-1,-1,-1,	// '=', 3A, 3B, 3C, |3D|, 3E, 3F, 40
		0,1,2,3,4,5,6,7,8,9,	// 'A'-'Z', 41 - 5A
		10,11,12,13,14,15,16,17,18,19,
		20,21,22,23,24,25,		
		-1,-1,-1,-1,-1,-1,		// 5B 5C 5D 5E 5F 60
		26,27,28,29,30,31,32,33,34,35, // 'a'-'z' 61 - 7A
		36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51
	};

	LPBYTE p = (LPBYTE)pDataOut;

	int a[4];

	for(;str_len>4;str_len-=4,pBase64+=4,p+=3)
	{
		if(rt::IsInRange_CC<int>(pBase64[0],0x2b,0x7a) && (a[0]=rev_table[pBase64[0]-0x2b])>=0){}
		else break;
		if(rt::IsInRange_CC<int>(pBase64[1],0x2b,0x7a) && (a[1]=rev_table[pBase64[1]-0x2b])>=0){}
		else break;
		if(rt::IsInRange_CC<int>(pBase64[2],0x2b,0x7a) && (a[2]=rev_table[pBase64[2]-0x2b])>=0){}
		else break;
		if(rt::IsInRange_CC<int>(pBase64[3],0x2b,0x7a) && (a[3]=rev_table[pBase64[3]-0x2b])>=0){}
		else break;

		p[0] = (a[0]<<2) | (a[1]>>4);
		p[1] = ((a[1]&0xf)<<4) | (a[2]>>2);
		p[2] = ((a[2]&0x3)<<6) | a[3];
	}

	*pDataOutLen = (p - (LPBYTE)pDataOut);
	if(str_len == 0)return true;

	//int remaining = (int)rt::min(str_len, (SIZE_T)4);
	int miss = (4 - (str_len&3))&3;

	char last4char[4];
	*((DWORD*)last4char) = 0x3d3d3d3d; // all '='
	memcpy(last4char, pBase64, 4 - miss);

	if(rt::IsInRange_CC<int>(last4char[0],0x2b,0x7a) && (a[0]=rev_table[last4char[0]-0x2b])>=0){}
	else return false;
	if(rt::IsInRange_CC<int>(last4char[1],0x2b,0x7a) && (a[1]=rev_table[last4char[1]-0x2b])>=0){}
	else return false;

	if(_isb64(last4char[2]) && _isb64(last4char[3]))
	{
		(*pDataOutLen) += 3;

		a[2]=rev_table[last4char[2]-0x2b];
		a[3]=rev_table[last4char[3]-0x2b];

		p[0] = (a[0]<<2) | (a[1]>>4);
		p[1] = ((a[1]&0xf)<<4) | (a[2]>>2);
		p[2] = ((a[2]&0x3)<<6) | a[3];

		return str_len <= 4;
	}
	else if(_isb64(last4char[2]) && !_isb64(last4char[3]))
	{
		(*pDataOutLen) += 2;
		a[2]=rev_table[last4char[2]-0x2b];

		p[0] = (a[0]<<2) | (a[1]>>4);
		p[1] = ((a[1]&0xf)<<4) | (a[2]>>2);

		return str_len <= 4;
	}
	else if(!_isb64(last4char[2]) && !_isb64(last4char[3]))
	{
		(*pDataOutLen) += 1;
		
		p[0] = (a[0]<<2) | (a[1]>>4);

		return str_len <= 4;
	}

	return false;
}


SIZE_T os::Base16EncodeLength(SIZE_T len){ return len*2; }
SIZE_T os::Base16DecodeLength(SIZE_T len){ return len/2; }
void os::Base16Encode(LPSTR pBase16Out,LPCVOID pData_in, SIZE_T data_len)
{
	LPCBYTE pData = (LPCBYTE)pData_in;
	for(SIZE_T i=0;i<data_len;i++)
	{
		int c1 = pData[i]>>4;
		int c2 = pData[i]&0xf;
		pBase16Out[2*i+0] = (c1>9)?('A'+c1-10):('0'+c1);
		pBase16Out[2*i+1] = (c2>9)?('A'+c2-10):('0'+c2);
	}
}

bool os::Base16Decode(LPVOID pDataOut_in,SIZE_T data_len,LPCSTR pBase16, SIZE_T str_len)
{
	LPBYTE pDataOut = (LPBYTE)pDataOut_in;

	if(str_len != data_len*2)return false;
	for(SIZE_T i=0;i<data_len;i++)
	{
		int c[2] = { pBase16[2*i+0], pBase16[2*i+1] };

		for(int j=0;j<2;j++)
		{
			if(c[j]<='9')
			{	if(c[j]<'0')return false;
				c[j] -= '0';
			}
			else if(c[j]>='A' && c[j]<='F')
			{	c[j] -= 'A' - 10;
			}
			else if(c[j]>='a' && c[j]<='f')
			{	c[j] -= 'a' - 10;
			}
			else return false;
		}

		pDataOut[i] = (c[0]<<4) | c[1];
	}

	return true;
}

namespace os
{
namespace _details
{
	static const int _Base32_decode['z' - '0' + 1] = 
	{
		0,1,2,3,4, 5,6,7,8,9,	/*0-9*/
		-1,-1,-1,-1,-1,-1,-1,	/*:	; <	= >	? @	*/
		10,11,12,13,14,15,		/*A-F*/	
		16,17,					/* G H */
		1,						/* I = 1*/
		18,19,					/* J K */	
		1,						/*L	= 1*/
		20,21,					/*M	N */	
		0,						/*O = 0	*/
		22,						/*P	*/		
		23,24,25,26,			/*QRST */	
		27,27,					/*U V */	
		28,29,30,31,			/*WXYZ*/
		-1,-1,-1,-1,-1,-1,		/*[	\ ] ^ _ `*/	
		10,11,12,13,14,15,		/*a-f*/	
		16,17,					/* g h */
		1,						/* i = 1*/
		18,19,					/* j k */	
		1,						/*l	= 1*/
		20,21,					/*m	n */	
		0,						/*o = 0	*/
		22,						/*p	*/		
		23,24,25,26,			/*qrst */	
		27,27,					/*u v */	
		28,29,30,31,			/*wxyz*/
	};
	static const char _Base32_encode[33] = 
	//			1		  2			3
	//01234567890123456789012345678901
	 "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
	static const char _Base32_encode_lowercase[33] = "0123456789abcdefghjkmnpqrstvwxyz";
}
}

SIZE_T os::Base32DecodeLength(SIZE_T len)
{						  // 0, 1, 2, 3, 4, 5, 6, 7
	static const int tl[] = {  0,-1, 1,-1, 2, 3,-1, 4 } ;
	int tail_len = tl[len&0x7];
	if(tail_len >=0)
		return len/8*5 + tail_len;
	else
		return 0;
}

SIZE_T os::Base32EncodeLength(SIZE_T len)
{
	return (len*8+4)/5;// + 1;
}

void os::Base32Encode(rt::String& Base32Out, const rt::String_Ref& blob)
{
	VERIFY(Base32Out.SetLength(Base32EncodeLength(blob.GetLength())));
	Base32Encode(Base32Out.Begin(), blob.Begin(), blob.GetLength());
}

bool os::Base32Decode(rt::String& blob, const rt::String_Ref& base32in)
{
	VERIFY(blob.SetLength(Base32DecodeLength(base32in.GetLength())));
	return Base32Decode(blob.Begin(), blob.GetLength(), base32in.Begin(), base32in.GetLength());
}


// Crockford's Base32 - http://en.wikipedia.org/wiki/Base32
//value		encode	decode			value	encode	decode
//0			0		0 o O			16		G		g G
//1			1		1 i I l L		17		H		h H
//2			2		2				18		J		j J
//3			3		3				19		K		k K
//4			4		4				20		M		m M
//5			5		5				21		N		n N
//6			6		6				22		P		p P
//7			7		7				23		Q		q Q
//8			8		8				24		R		r R
//9			9		9				25		S		s S
//10		A		a A				26		T		t T
//11		B		b B				27		V		v V
//12		C		c C				28		W		w W
//13		D		d D				29		X		x X
//14		E		e E				30		Y		y Y
//15		F		f F				31		Z		z Z
void os::Base32Encode(LPSTR p,LPCVOID pdata, SIZE_T data_len)
{
	LPCBYTE d = (LPCBYTE)pdata;
	LPCBYTE e = d + data_len;
	for(;(d + 4)<e;d+=5, p+=8)
	{	
		p[0] = _details::_Base32_encode[d[0]>>3];
		p[1] = _details::_Base32_encode[((d[0]&0x7)<<2) | (d[1]>>6) ];
		p[2] = _details::_Base32_encode[(d[1]>>1) & 0x1f ];
		p[3] = _details::_Base32_encode[((d[1]&1)<<4) | (d[2]>>4) ];
		p[4] = _details::_Base32_encode[((d[2]&0xf)<<1) | (d[3]>>7) ];
		p[5] = _details::_Base32_encode[(d[3]>>2) & 0x1f  ];
		p[6] = _details::_Base32_encode[((d[3]&0x3)<<3) | (d[4]>>5) ];
		p[7] = _details::_Base32_encode[d[4]&0x1f];
	}

	if(d == e)return;

	p[0] = _details::_Base32_encode[d[0]>>3];
	if(d+1 == e)	// nnnXX
	{	static const char encode[9] = "JKMNQRST";	
		p[1] = encode[d[0]&0x7];
		return;
	}

	p[1] = _details::_Base32_encode[((d[0]&0x7)<<2) | (d[1]>>6) ];
	p[2] = _details::_Base32_encode[(d[1]>>1) & 0x1f ];
	if(d+2 == e)	// nXXXX
	{	static const char encode[3] = "GH";	
		p[3] = encode[d[1]&0x1];
		return;
	}

	p[3] = _details::_Base32_encode[((d[1]&1)<<4) | (d[2]>>4) ];
	if(d+3 == e)	// nnnnX
	{	p[4] = _details::_Base32_encode[d[2]&0xf];
		return;
	}

	p[4] = _details::_Base32_encode[((d[2]&0xf)<<1) | (d[3]>>7) ];
	p[5] = _details::_Base32_encode[(d[3]>>2) & 0x1f  ];
	ASSERT(d+4 == e);	// nnXXX
	p[6] = (d[3]&0x3) + 'W';

	return;
}

void os::Base32EncodeLowercase(LPSTR p,LPCVOID pdata, SIZE_T data_len)
{
	LPCBYTE d = (LPCBYTE)pdata;
	LPCBYTE e = d + data_len;
	for(;(d + 4)<e;d+=5, p+=8)
	{	
		p[0] = _details::_Base32_encode_lowercase[d[0]>>3];
		p[1] = _details::_Base32_encode_lowercase[((d[0]&0x7)<<2) | (d[1]>>6) ];
		p[2] = _details::_Base32_encode_lowercase[(d[1]>>1) & 0x1f ];
		p[3] = _details::_Base32_encode_lowercase[((d[1]&1)<<4) | (d[2]>>4) ];
		p[4] = _details::_Base32_encode_lowercase[((d[2]&0xf)<<1) | (d[3]>>7) ];
		p[5] = _details::_Base32_encode_lowercase[(d[3]>>2) & 0x1f  ];
		p[6] = _details::_Base32_encode_lowercase[((d[3]&0x3)<<3) | (d[4]>>5) ];
		p[7] = _details::_Base32_encode_lowercase[d[4]&0x1f];
	}

	if(d == e)return;

	p[0] = _details::_Base32_encode_lowercase[d[0]>>3];
	if(d+1 == e)	// nnnXX
	{	static const char encode[9] = "jkmnqrst";	
		p[1] = encode[d[0]&0x7];
		return;
	}

	p[1] = _details::_Base32_encode_lowercase[((d[0]&0x7)<<2) | (d[1]>>6) ];
	p[2] = _details::_Base32_encode_lowercase[(d[1]>>1) & 0x1f ];
	if(d+2 == e)	// nXXXX
	{	static const char encode[3] = "gh";	
		p[3] = encode[d[1]&0x1];
		return;
	}

	p[3] = _details::_Base32_encode_lowercase[((d[1]&1)<<4) | (d[2]>>4) ];
	if(d+3 == e)	// nnnnX
	{	p[4] = _details::_Base32_encode_lowercase[d[2]&0xf];
		return;
	}

	p[4] = _details::_Base32_encode_lowercase[((d[2]&0xf)<<1) | (d[3]>>7) ];
	p[5] = _details::_Base32_encode_lowercase[(d[3]>>2) & 0x1f  ];
	ASSERT(d+4 == e);	// nnXXX
	p[6] = (d[3]&0x3) + 'w';

	return;

}

bool os::Base32Decode(LPVOID pDataOut,SIZE_T data_len,LPCSTR pBase32, SIZE_T str_len)
{
	if(Base32DecodeLength(str_len) != data_len)return false;

	LPBYTE p = (LPBYTE)pDataOut;
	LPCSTR d = pBase32;
	LPCSTR e = d + str_len;
	for(;(d + 8)<=e; p+=5)
	{
		int b[8];
		if(*d>='0' && *d<='z'){ b[0] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[1] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[2] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[3] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[4] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[5] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[6] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[7] = _details::_Base32_decode[(*d++) - '0']; }else return false;

		p[0] = (b[0]<<3) | (b[1]>>2);
		p[1] = (b[1]<<6) | (b[2]<<1) | (b[3]>>4);
		p[2] = (b[3]<<4) | (b[4]>>1);
		p[3] = (b[4]<<7) | (b[5]<<2) | (b[6]>>3);
		p[4] = (b[6]<<5) | b[7];
	}

	if(d == e)return true;

	int b[7];
	if(*d>='0' && *d<='z' && d<e){ b[0] = _details::_Base32_decode[(*d++) - '0']; }else return false;

	if(d+1 == e)					 //J K L,M N O,P,Q R S T
	{	static const int decode[11] = {0,1,-1,2,3,-1,-1,4,5,6,7};
		int v;
		if(d[0]>='J' && d[0]<='T')
		{	if((v = decode[d[0]-'J']) == -1)return false; }
		else if(d[0]>='j' && d[0]<='t')
		{	if((v = decode[d[0]-'j']) == -1)return false; }
		else return false;

		p[0] = (b[0]<<3) | v;
		return true;
	}
	
	if(*d>='0' && *d<='z' && d<e){ b[1] = _details::_Base32_decode[(*d++) - '0']; }else return false;
	p[0] = (b[0]<<3) | (b[1]>>2);
	if(*d>='0' && *d<='z' && d<e){ b[2] = _details::_Base32_decode[(*d++) - '0']; }else return false;

	if(d+1 == e)
	{	int v;
		if(d[0] == 'g' || d[0] == 'G'){ v = 0; }
		else if(d[0] == 'h' || d[0] == 'H'){ v = 1; }
		else{ return false; }
		p[1] = (b[1]<<6) | (b[2]<<1) | v;
		return true;
	}
	
	if(*d>='0' && *d<='z' && d<e){ b[3] = _details::_Base32_decode[(*d++) - '0']; }else return false;
	p[1] = (b[1]<<6) | (b[2]<<1) | (b[3]>>4);

	if(d+1 == e)
	{	if(d[0]<'0' || d[0]>'z')return false;
		int v = _details::_Base32_decode[d[0] - '0'];
		if(v<0 || v>15)return false;
		p[2] = (b[3]<<4) | v;
		return true;
	}

	if(*d>='0' && *d<='z' && d<e){ b[4] = _details::_Base32_decode[(*d++) - '0']; }else return false;
	p[2] = (b[3]<<4) | (b[4]>>1);
	if(*d>='0' && *d<='z' && d<e){ b[5] = _details::_Base32_decode[(*d++) - '0']; }else return false;

	if(d+1 == e)
	{	if(d[0]<'0' || d[0]>'z')return false;
		int v = _details::_Base32_decode[d[0] - '0'];
		if(v<28)return false;
		p[3] = (b[4]<<7) | (b[5]<<2) | (v-28);
		return true;
	}

	return false;
}


bool os::Base32DecodeLowercase(LPVOID pDataOut,SIZE_T data_len,LPCSTR pBase32, SIZE_T str_len)
{
	LPBYTE p = (LPBYTE)pDataOut;
	LPCSTR d = pBase32;
	LPCSTR e = d + str_len;
	for(;(d + 8)<=e; p+=5)
	{
		int b[8];
		if(*d>='0' && *d<='z'){ b[0] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[1] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[2] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[3] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[4] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[5] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[6] = _details::_Base32_decode[(*d++) - '0']; }else return false;
		if(*d>='0' && *d<='z'){ b[7] = _details::_Base32_decode[(*d++) - '0']; }else return false;

		p[0] = (b[0]<<3) | (b[1]>>2);
		p[1] = (b[1]<<6) | (b[2]<<1) | (b[3]>>4);
		p[2] = (b[3]<<4) | (b[4]>>1);
		p[3] = (b[4]<<7) | (b[5]<<2) | (b[6]>>3);
		p[4] = (b[6]<<5) | b[7];
	}

	if(d == e)return true;

	int b[7];
	if(*d>='0' && *d<='z' && d<e){ b[0] = _details::_Base32_decode[(*d++) - '0']; }else return false;

	if(d+1 == e)					 //J K L,M N O,P,Q R S T
	{	static const int decode[11] = {0,1,-1,2,3,-1,-1,4,5,6,7};
		int v;
		if(d[0]<'j' || d[0]>'t' || (v = decode[d[0]-'j']) == -1)return false;
		p[0] = (b[0]<<3) | v;
		return true;
	}
	
	if(*d>='0' && *d<='z' && d<e){ b[1] = _details::_Base32_decode[(*d++) - '0']; }else return false;
	p[0] = (b[0]<<3) | (b[1]>>2);
	if(*d>='0' && *d<='z' && d<e){ b[2] = _details::_Base32_decode[(*d++) - '0']; }else return false;

	if(d+1 == e)
	{	int v;
		if(d[0] == 'g'){ v = 0; }
		else if(d[0] == 'h'){ v = 1; }
		else{ return false; }
		p[1] = (b[1]<<6) | (b[2]<<1) | v;
		return true;
	}
	
	if(*d>='0' && *d<='z' && d<e){ b[3] = _details::_Base32_decode[(*d++) - '0']; }else return false;
	p[1] = (b[1]<<6) | (b[2]<<1) | (b[3]>>4);

	if(d+1 == e)
	{	if(d[0]<'0' || d[0]>'z')return false;
		int v = _details::_Base32_decode[d[0] - '0'];
		if(v<0 || v>15)return false;
		p[2] = (b[3]<<4) | v;
		return true;
	}

	if(*d>='0' && *d<='z' && d<e){ b[4] = _details::_Base32_decode[(*d++) - '0']; }else return false;
	p[2] = (b[3]<<4) | (b[4]>>1);
	if(*d>='0' && *d<='z' && d<e){ b[5] = _details::_Base32_decode[(*d++) - '0']; }else return false;

	if(d+1 == e)
	{	if(d[0]<'0' || d[0]>'z')return false;
		int v = _details::_Base32_decode[d[0] - '0'];
		if(v<28)return false;
		p[3] = (b[4]<<7) | (b[5]<<2) | (v-28);
		return true;
	}

	return false;
}


void os::UrlEncode(const rt::String_Ref& url, rt::String& encoded_url)
{
	encoded_url.SetLength(url.GetLength()*3);
	encoded_url.SetLength(UrlEncode((LPCSTR)url.Begin(), (UINT)url.GetLength(), (LPSTR)encoded_url.Begin()));
}

UINT os::UrlEncode(LPCSTR url, UINT url_len, LPSTR encoded_url)
{
	UINT out_len = 0;
	for(UINT i=0;i<url_len;i++)
	{
		if(	(url[i]>='a' && url[i]<='z') ||
			(url[i]>='A' && url[i]<='Z') ||
			(url[i]>='0' && url[i]<='9') ||
			url[i] == '-' ||
			url[i] == '_' ||
			url[i] == '.' ||
			url[i] == '~'
		)
		{	encoded_url[out_len++] = url[i];
		}
		else
		{	encoded_url[out_len++] = '%';

			rt::tos::Binary<> code(&url[i], 1);

			encoded_url[out_len++] = code[0];
			encoded_url[out_len++] = code[1];
		}
	}

	return out_len;
}

UINT os::UrlDecode(LPCSTR encoded_url, UINT encoded_url_len, LPSTR url)  // return encoded length
{
	UINT out_len = 0;
	for(UINT i=0;i<encoded_url_len;i++)
	{
		if(encoded_url[i] == '%')
		{
			int c1 = encoded_url[i+1];
			int c2 = encoded_url[i+2];

			if(c1>='0' && c1<='9')
				c1 -= '0';
			else if(c1>='A' && c1<='F')
				c1 -= c1-'A' + 10;
			else if(c1>='a' && c1<='f')
				c1 -= c1-'a' + 10;
			else break;

			if(c2>='0' && c2<='9')
				c2 -= '0';
			else if(c2>='A' && c2<='F')
				c2 = c2-'A' + 10;
			else if(c2>='a' && c2<='f')
				c2 = c2-'a' + 10;
			else break;

			url[out_len++] = (c1<<4) | c2;
			i+=2;
		}
		else
		{	url[out_len++] = encoded_url[i];
		}
	}

	return out_len;
}

void os::UrlDecode(const rt::String_Ref& encoded_url, rt::String& url)
{
	url.SetLength(encoded_url.GetLength());
	url.SetLength(UrlDecode(encoded_url.Begin(), (UINT)encoded_url.GetLength(), url.Begin()));
}



#if defined(PLATFORM_IOS)

extern "C" void _setup_debug_textbox(LPVOID pView);
extern "C" void _set_debug_textbox(LPCSTR text);

void os::SetDebugTextBox(const rt::String_Ref& x)
{
    if(x.IsEmpty())_set_debug_textbox("");
    else if(x.IsZeroTerminated())_set_debug_textbox(x.Begin());
    else
    {   _set_debug_textbox(rt::String(x));
    }
}
void os::SetupDebugTextBox(LPVOID param){ _setup_debug_textbox(param); }

#else

void os::SetDebugTextBox(const rt::String_Ref& x)
{
	_LOG("DebugTextBox: \n"<<x);
}

void os::SetupDebugTextBox(LPVOID param){}

#endif


