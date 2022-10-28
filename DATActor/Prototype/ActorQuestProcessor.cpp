#include <iostream>
#include "ActorQuestProcessor.h"

using namespace std;

FPAnswerPtr ActorQuestProcessor::action(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	int taskId = args->wantInt("taskId");
	std::string method = args->wantString("method");
	std::string payload = args->wantString("payload");

	cout<<"[Info] This is prototype actor. Don't do anything."<<endl;
	cout<<"[Info] Quest Method: "<<quest->method()<<endl;
	cout<<"[Info] Actor Method: "<<method<<endl;
	cout<<"[Info] Actor taskId: "<<taskId<<endl;
	cout<<"[Info] Payload size: "<<payload.size()<<endl<<endl;

	return FPAWriter::emptyAnswer(quest);
}