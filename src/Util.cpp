#include "Util.h"

CRITICAL_SECTION g_cs_log;


DWORD CountSetBits(ULONG_PTR bitMask)
{
	DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
	DWORD bitSetCount = 0;
	ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;    
	DWORD i;

	for (i = 0; i <= LSHIFT; ++i)
	{
		bitSetCount += ((bitMask & bitTest)?1:0);
		bitTest/=2;
	}
	return bitSetCount;
}

int GetProccessNum()
{
	SYSTEM_INFO siSysInfo; 
	GetSystemInfo(&siSysInfo); 
	return siSysInfo.dwNumberOfProcessors;
}

typedef BOOL (WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

void GetProcessNum(int & logicNum, int & procCore)
{
	LPFN_GLPI glpi;
	glpi = (LPFN_GLPI) GetProcAddress(GetModuleHandle(TEXT("kernel32")),"GetLogicalProcessorInformation");
	if (NULL == glpi) 
	{
		log("GetLogicalProcessorInformation is not supported.\n");
		goto fail;
	}
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
	DWORD returnLength = 0;

	glpi(0, &returnLength);
	ptr = buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION) malloc(returnLength);
	if (NULL == buffer) 
	{
		log("\nError: Allocation failure\n");
		goto fail;
	}
	DWORD rc = glpi(buffer, &returnLength);
	if (FALSE == rc) 
	{
		log("Error %d\n", GetLastError());
		goto fail;
	}

	DWORD byteOffset = 0;
	DWORD logicalProcessorCount = 0;
	DWORD processorCoreCount = 0;

	while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength) 
	{
		switch (ptr->Relationship) 
		{
		case RelationProcessorCore:
			processorCoreCount++;
			logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
			break;
		}
		byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		ptr++;
	}
	logicNum = logicalProcessorCount;
	procCore = processorCoreCount;
	if(buffer) free(buffer);
	return;
fail:
	logicNum = procCore = GetProccessNum();
	if(buffer) free(buffer);
}

