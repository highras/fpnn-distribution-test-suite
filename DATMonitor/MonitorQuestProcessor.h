#ifndef DAT_Monitor_Quest_Processor_h
#define DAT_Monitor_Quest_Processor_h

#include "IQuestProcessor.h"

using namespace fpnn;

class MonitorQuestProcessor: public IQuestProcessor
{
	QuestProcessorClassPrivateFields(MonitorQuestProcessor)

public:
	MonitorQuestProcessor()
	{
		registerMethod("machineStatus", &MonitorQuestProcessor::machineStatus);
		registerMethod("ping", &MonitorQuestProcessor::ping);
	}

	FPAnswerPtr machineStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr ping(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
	{
		return FPAWriter::emptyAnswer(quest);
	}

	QuestProcessorClassBasicPublicFuncs
};

#endif