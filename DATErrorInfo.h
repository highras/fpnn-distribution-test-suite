#ifndef DAT_Error_Info_h
#define DAT_Error_Info_h

#include "FPWriter.h"

namespace ErrorInfo
{
	using namespace fpnn;

	const int errorBase = 10000 * 10;

	const int ActorHostBusyCode = errorBase + 1;
	const int FileUploadTaskExistCode = errorBase + 2;
	const int ActorIsNotExistCode = errorBase + 3;
}

#endif
