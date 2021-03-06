#pragma once

//////////////////////////////////////////////////////////////////////
// Cross-Platform Core Foundation (CPCF)
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of CPF.  nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
//  Macros to control linkage
//
//	Win32:
//	IPP_LINK_STATIC_LIB_PX - C-optimized for all IA-32 processors 
//	IPP_LINK_STATIC_LIB_W7 - Optimized for Pentium 4 processors 
//	IPP_LINK_STATIC_LIB_V8 - Intel Core2Duo processors
//	IPP_LINK_STATIC_LIB_P8 - Intel Core2Duo processors (Penryn)
//  IPP_LINK_STATIC_LIB_H9 - 
//	IPP_LINK_STATIC_LIB_G9 - 
//
//	Win64:
//	IPP_LINK_STATIC_LIB_MX - C-optimized for all Intel Xeon processors with Intel Extended Memory 64 Technology
//	IPP_LINK_STATIC_LIB_M7 - Optimized for Intel Xeon processors with Intel Extended Memory 64 Technology
//	IPP_LINK_STATIC_LIB_U8
//
//////////////////////////////////////////////////////////////////////


//IA-32
//px	C optimized for all IA-32 processors	i386+	
//a6	SSE	Pentium III	thru 5.3 only
//w7	SSE2	P4, Xeon, Centrino	
//t7	SSE3	Prescott, Yonah	
//v8	Supplemental SSE3	Core 2, Xeon?5100, Atom	
//s8	Supplemental SSE3 (compiled for Atom)	Atom	new in 6.0
//p8	SSE4.1, SSE4.2, AES-NI	Penryn, Nehalem, Westmere	see notes below
//g9	AVX	Sandy Bridge Architecture	new in 6.1
//
//Intel?64 (EM64T)
//mx	C-optimized for all Intel?64 platforms	P4	SSE2 minimum
//m7	SSE3	Prescott	
//u8	Supplemental SSE3	Core 2, Xeon?5100, Atom	
//n8	Supplemental SSE3 (compiled for Atom)	Atom	new in 6.0
//y8	SSE4.1, SSE4.2, AES-NI	Penryn, Nehalem, Westmere	see notes below
//e9	AVX	Sandy Bridge Architecture	new in 6.1



#include "../../os/predefines.h"


#define PNG_USE_GLOBAL_ARRAYS
#define PNG_SETJMP_SUPPORTED
// #define NEED_SIGNAL_CATCHER for libjpg


#ifdef PLATFORM_INTEL_IPP_SUPPORT

/*
#ifdef _WIN64
#define IPP_LINK_STATIC_LIB_Y8
#else
#define IPP_LINK_STATIC_LIB_P8
#endif
*/

#if defined(PLATFORM_WIN) 
#define _IPP_SEQUENTIAL_STATIC
#endif

#define _IPP_NO_DEFAULT_LIB

#pragma warning(disable:4819)


#if defined(IPP_LINK_STATIC_LIB_W7)
	#define IPPAPI(type,name,arg) extern type __STDCALL w7_##name arg;
	#define IPPCALL(name) w7_##name
#elif defined(IPP_LINK_STATIC_LIB_V8)
	#define IPPAPI(type,name,arg) extern type __STDCALL v8_##name arg;
	#define IPPCALL(name) v8_##name
#elif defined(IPP_LINK_STATIC_LIB_P8)
	#define IPPAPI(type,name,arg) extern type __STDCALL p8_##name arg;
	#define IPPCALL(name) p8_##name
#elif defined(IPP_LINK_STATIC_LIB_MX)
	#define IPPAPI(type,name,arg) extern type __STDCALL mx_##name arg;
	#define IPPCALL(name) mx_##name
#elif defined(IPP_LINK_STATIC_LIB_M7)
	#define IPPAPI(type,name,arg) extern type __STDCALL m7_##name arg;
	#define IPPCALL(name) m7_##name
#elif defined(IPP_LINK_STATIC_LIB_Y8)
	#define IPPAPI(type,name,arg) extern type __STDCALL y8_##name arg;
	#define IPPCALL(name) y8_##name
#else
	#define IPP_LINK_STATIC_LIB_PX
	#define IPPAPI(type,name,arg) extern type __STDCALL name arg;
	#define IPPCALL(name) name
#endif

#define IPPAPI_NOPREFIX(type,name,arg) extern type __STDCALL name arg;

#if defined(PLATFORM_WIN)

#ifdef _WIN64
	#pragma comment(lib,"ipp_x64_ippac_l.lib")
	#pragma comment(lib,"ipp_x64_ippcc_l.lib")
	#pragma comment(lib,"ipp_x64_ippch_l.lib")
	#pragma comment(lib,"ipp_x64_ippcore_l.lib")
	#pragma comment(lib,"ipp_x64_ippcp_l.lib")
	#pragma comment(lib,"ipp_x64_ippcv_l.lib")
	#pragma comment(lib,"ipp_x64_ippdc_l.lib")
	#pragma comment(lib,"ipp_x64_ippdi_l.lib")
	#pragma comment(lib,"ipp_x64_ippgen_l.lib")
	#pragma comment(lib,"ipp_x64_ippi_l.lib")
	#pragma comment(lib,"ipp_x64_ippj_l.lib")
	#pragma comment(lib,"ipp_x64_ippm_l.lib")
	#pragma comment(lib,"ipp_x64_ippr_l.lib")
	#pragma comment(lib,"ipp_x64_ippsc_l.lib")
	#pragma comment(lib,"ipp_x64_ipps_l.lib")
	#pragma comment(lib,"ipp_x64_ippvc_l.lib")
	#pragma comment(lib,"ipp_x64_ippvm_l.lib")
#else
	#pragma comment(lib,"ipp_w32_ippac_l.lib")
	#pragma comment(lib,"ipp_w32_ippcc_l.lib")
	#pragma comment(lib,"ipp_w32_ippch_l.lib")
	#pragma comment(lib,"ipp_w32_ippcore_l.lib")
	#pragma comment(lib,"ipp_w32_ippcp_l.lib")
	#pragma comment(lib,"ipp_w32_ippcv_l.lib")
	#pragma comment(lib,"ipp_w32_ippdc_l.lib")
	#pragma comment(lib,"ipp_w32_ippdi_l.lib")
	#pragma comment(lib,"ipp_w32_ippgen_l.lib")
	#pragma comment(lib,"ipp_w32_ippi_l.lib")
	#pragma comment(lib,"ipp_w32_ippj_l.lib")
	#pragma comment(lib,"ipp_w32_ippm_l.lib")
	#pragma comment(lib,"ipp_w32_ippr_l.lib")
	#pragma comment(lib,"ipp_w32_ippsc_l.lib")
	#pragma comment(lib,"ipp_w32_ipps_l.lib")
	#pragma comment(lib,"ipp_w32_ippvc_l.lib")
	#pragma comment(lib,"ipp_w32_ippvm_l.lib")
#endif

#endif // #if defined(PLATFORM_WIN)

#endif // #ifdef PLATFORM_INTEL_IPP_SUPPORT
