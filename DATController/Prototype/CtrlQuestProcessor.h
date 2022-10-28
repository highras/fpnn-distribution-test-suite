#ifndef Controller_Quest_Processor_h
#define Controller_Quest_Processor_h

#include "IQuestProcessor.h"

using namespace fpnn;

class CtrlQuestProcessor: public IQuestProcessor
{
	QuestProcessorClassPrivateFields(CtrlQuestProcessor)

public:
	CtrlQuestProcessor()
	{
		registerMethod("uploadFinish", &CtrlQuestProcessor::uploadFinish);
		registerMethod("deplayFinish", &CtrlQuestProcessor::deplayFinish);
		registerMethod("actorStatus", &CtrlQuestProcessor::actorStatus);
		registerMethod("actorResult", &CtrlQuestProcessor::actorResult);
	}

	FPAnswerPtr uploadFinish(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr deplayFinish(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr actorStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr actorResult(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);

	QuestProcessorClassBasicPublicFuncs
};

#endif