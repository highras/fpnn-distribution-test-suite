#include <iostream>
#include "CtrlQuestProcessor.h"

using namespace std;

FPAnswerPtr CtrlQuestProcessor::uploadFinish(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	int taskId = args->wantInt("taskId");
	std::string actor = args->wantString("actor");
	bool ok = args->wantBool("ok");

	cout<<"[Info] This is prototype actor. Don't do anything."<<endl;
	cout<<"[Info] Quest Method: "<<quest->method()<<endl;
	cout<<"[Info] Actor: "<<actor<<endl;
	cout<<"[Info] Actor taskId: "<<taskId<<endl;
	cout<<"[Info] Upload status: "<<ok<<endl<<endl;

	return FPAWriter::emptyAnswer(quest);
}

FPAnswerPtr CtrlQuestProcessor::deplayFinish(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	int taskId = args->wantInt("taskId");
	std::vector<std::string> failedEndpoints = args->want("failedEndpoints", std::vector<std::string>());

	cout<<"[Info] This is prototype actor. Don't do anything."<<endl;
	cout<<"[Info] Quest Method: "<<quest->method()<<endl;
	cout<<"[Info] Actor taskId: "<<taskId<<endl;
	cout<<"[Info] Failed endpoints count: "<<failedEndpoints.size()<<endl<<endl;

	return FPAWriter::emptyAnswer(quest);
}

FPAnswerPtr CtrlQuestProcessor::actorStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	int taskId = args->wantInt("taskId");
	std::string region = args->wantString("region");
	std::string endpoint = args->wantString("endpoint");
	std::string payload = args->wantString("payload");

	cout<<"[Info] This is prototype controller. Don't do anything."<<endl;
	cout<<"[Info] Quest Method: "<<quest->method()<<endl;
	cout<<"[Info] Actor region: "<<region<<endl;
	cout<<"[Info] Actor endpoint: "<<endpoint<<endl;
	cout<<"[Info] Actor taskId: "<<taskId<<endl;
	cout<<"[Info] Payload size: "<<payload.size()<<endl<<endl;

	return FPAWriter::emptyAnswer(quest);
}

FPAnswerPtr CtrlQuestProcessor::actorResult(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	int taskId = args->wantInt("taskId");
	std::string region = args->wantString("region");
	std::string endpoint = args->wantString("endpoint");
	std::string payload = args->wantString("payload");

	cout<<"[Info] This is prototype controller. Don't do anything."<<endl;
	cout<<"[Info] Quest Method: "<<quest->method()<<endl;
	cout<<"[Info] Actor region: "<<region<<endl;
	cout<<"[Info] Actor endpoint: "<<endpoint<<endl;
	cout<<"[Info] Actor taskId: "<<taskId<<endl;
	cout<<"[Info] Payload size: "<<payload.size()<<endl<<endl;

	return FPAWriter::emptyAnswer(quest);
}