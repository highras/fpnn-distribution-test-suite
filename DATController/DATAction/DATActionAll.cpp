#include <iostream>
#include <vector>
#include "TCPClient.h"

using namespace std;
using namespace fpnn;

int findIndex(const std::string& field, const std::vector<std::string>& fields)
{
	for (size_t i = 0; i < fields.size(); i++)
		if (fields[i] == field)
			return (int)i;

	return -1;
}

void sendAction(TCPClientPtr client, const std::string& actorName, const std::string& actorEndpoint,
	int pid, const std::string& method, const std::string& payload, const std::string& desc)
{
	FPWriter pw(payload);
	FPQWriter qw(6, "actorAction");
	qw.param("actor", actorName);
	qw.param("endpoint", actorEndpoint);
	qw.param("pid", pid);
	qw.param("method", method);
	qw.param("payload", pw.raw());
	qw.param("taskDesc", desc);
	FPQuestPtr quest = qw.take();

	FPAnswerPtr answer = client->sendQuest(quest);
	FPAReader ar(answer);
	if (answer->status())
	{
		cout<<"[Exception] actor endpoint: "<<actorEndpoint<<", pid: "<<pid;
		cout<<", error code: "<<ar.getInt("code")<<", ex: "<<ar.getString("ex")<<endl;
	}
	else
		cout<<"Task id: "<<ar.wantInt("taskId")<<". Endpoint: "<<actorEndpoint<<", pid: "<<pid<<endl;
}

int main(int argc, char* argv[])
{
	if (argc != 5 && argc != 6)
	{   
		cout<<"Usage: "<<argv[0]<<" controlCenterEndpoint actor method payload(json) [desc]"<<endl;
		return 0;
	}   
	string ccep = argv[1];
	string actorName = argv[2];
	string method = argv[3];
	string payload = argv[4];
	string desc;

	if (argc == 6)
		desc = argv[5];

	std::shared_ptr<TCPClient> client = TCPClient::createClient(ccep);
	FPAnswerPtr answer = client->sendQuest(FPQWriter::emptyQuest("actorTaskStatus"));
	FPAReader ar(answer);
	if (ar.status())
		cout<<"Return:"<<answer->json()<<endl;
	else
	{
		std::vector<std::string> fields = ar.want("fields", std::vector<std::string>());
		std::vector<std::vector<std::string>> rows = ar.want("rows", std::vector<std::vector<std::string>>());
			
		int nameIdx = findIndex("actorName", fields);
		int epIdx = findIndex("endpoint", fields);
		int pidIdx = findIndex("pid", fields);

		for (auto& row: rows)
		{
			if (row[nameIdx] == actorName)
			{
				int pid = atoi(row[pidIdx].c_str());
				sendAction(client, actorName, row[epIdx], pid, method, payload, desc);
			}
		}
	}

	client->close();
	return 0;
}

