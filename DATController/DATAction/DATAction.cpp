#include <iostream>
#include <vector>
#include <thread>
#include <assert.h>
#include "TCPClient.h"
#include "IQuestProcessor.h"

using namespace std;
using namespace fpnn;

int main(int argc, char* argv[])
{
	if (argc != 7 && argc != 8)
	{   
		cout<<"Usage: "<<argv[0]<<" controlCenterEndpoint actor actorEndpoint pid method payload(json) [desc]"<<endl;
		return 0;
	}   
	string ccep = argv[1];
	string actor = argv[2];
	string aep = argv[3];
	int pid = atoi(argv[4]);
	string method = argv[5];
	string payload = argv[6];
	string desc;

	if (argc == 8)
		desc = argv[7];

	FPWriter pw(payload);
	FPQWriter qw(6, "actorAction");
	qw.param("actor", actor);
	qw.param("endpoint", aep);
	qw.param("pid", pid);
	qw.param("method", method);
	qw.param("payload", pw.raw());
	qw.param("taskDesc", desc);
	FPQuestPtr quest = qw.take();

	std::shared_ptr<TCPClient> client = TCPClient::createClient(ccep);

	FPAnswerPtr answer = client->sendQuest(quest);
	cout<<"Return:"<<answer->json()<<endl;
	client->close();
	return 0;
}

