// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include <crtdbg.h>
#include "memory_mgmt.h"
#include <iostream>

static DWORD tlsIndex = TLS_OUT_OF_INDEXES;

MemoryPools* getMemoryPoolsInst()
{
	return static_cast<MemoryPools*>(TlsGetValue(tlsIndex));
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		tlsIndex = TlsAlloc();
		//std::cout << "doing DLL_PROCESS_ATTACH, tlsIndex = " << tlsIndex << std::endl;
		if(TLS_OUT_OF_INDEXES == tlsIndex) {
			std::cout << "Failed allocationg tls index" << std::endl;
			return FALSE;
		}
    case DLL_THREAD_ATTACH:
		TlsSetValue(tlsIndex, makeMemoryPoolsInst());
		//std::cout << "doing DLL_THREAD_ATTACH" << std::endl;
		break;
	case DLL_THREAD_DETACH:
		//std::cout << "doing DLL_THREAD_DETACH" << std::endl;
	case DLL_PROCESS_DETACH:
		freeMemoryPoolsInst(getMemoryPoolsInst());
		TlsSetValue(tlsIndex, nullptr);
		if (DLL_PROCESS_DETACH == ul_reason_for_call) {
			//std::cout << "doing DLL_PROCESS_DETACH" << std::endl;
			TlsFree(tlsIndex);
			tlsIndex = TLS_OUT_OF_INDEXES;
		}
		break;
    }
    return TRUE;
}

