#ifndef Actor_Quest_Processor_h
#define Actor_Quest_Processor_h

#include "IQuestProcessor.h"

using namespace fpnn;

class ActorQuestProcessor: public IQuestProcessor
{
	QuestProcessorClassPrivateFields(ActorQuestProcessor)

public:
	ActorQuestProcessor()
	{
		registerMethod("action", &ActorQuestProcessor::action);
	}

	FPAnswerPtr action(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);

	QuestProcessorClassBasicPublicFuncs
};

#endif