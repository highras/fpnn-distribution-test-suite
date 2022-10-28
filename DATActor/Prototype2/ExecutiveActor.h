#ifndef Base_Actor_h
#define Base_Actor_h

#include "TCPClient.h"

using namespace fpnn;

/*
	Class ControlCenter is assistant class. Just use it directly.
*/
class ControlCenter
{
public:
	static void beginTask(int taskId, const std::string& method, const std::string& desc);
	static void finishTask(int taskId);

	static FPAnswerPtr sendQuest(FPQuestPtr quest, int timeout = 0);
	static bool sendQuest(FPQuestPtr quest, AnswerCallback* callback, int timeout = 0);
	static bool sendQuest(FPQuestPtr quest, std::function<void (FPAnswerPtr answer, int errorCode)> task, int timeout = 0);
};

/*
	Just rewrite the following class.
	The default three methods are interfaces of executive actor, which cannot be removed.
*/
class ExecutiveActor
{
public:
	bool globalInit();
	bool actorStopped();
	std::string actorName();
	void setRegion(const std::string& region) {}
	void action(int taskId, const std::string& method, const FPReaderPtr payload);
	static std::string customParamsUsage() { return ""; }
};


#endif