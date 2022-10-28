#include "ExecutiveActor.h"

bool ExecutiveActor::globalInit()
{
	ClientEngine::configQuestProcessThreadPool(0, 1, 2, 6, 0);
	return true;
}

bool ExecutiveActor::actorStopped()
{
	return false;	//-- forever running.
}

std::string ExecutiveActor::actorName()
{
	return "Demo Actor";
}

void ExecutiveActor::action(int taskId, const std::string& method, const FPReaderPtr payload)
{
}