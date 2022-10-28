#include <iostream>
#include "FormattedPrint.h"
#include "TCPClient.h"

using namespace std;
using namespace fpnn;

int showUsage(const char* appName)
{
	cout<<"Usgae:"<<endl;
	cout<<"\t"<<appName<<" endpoint"<<endl;
	cout<<"\t"<<appName<<" host port"<<endl;
	return -1;
}

void formatMTime(std::vector<std::string>& fields, std::vector<std::vector<std::string>>& rows)
{
	size_t idx = 0;
	for (; idx < fields.size(); idx++)
		if (fields[idx] == "mtime")
		{
			fields[idx] = "mtime (UTC)";
			break;
		}

	if (idx < fields.size())
		for (auto& row: rows)
			if (row[idx].size())
			{
				time_t mtime = (time_t)(atol(row[idx].c_str()));
				row[idx] = ctime(&mtime);		//-- This function is not thread-safe, but all ctime() called are in main thread.
				row[idx] = StringUtil::trim(row[idx]);
			}
}

void showStatusObject(OBJECT& object, const char* title)
{
	FPReader ar(object);
	std::vector<std::string> fields = ar.want("fields", std::vector<std::string>());
	std::vector<std::vector<std::string>> rows = ar.want("rows", std::vector<std::vector<std::string>>());
	formatMTime(fields, rows);
	cout<<title<<endl;
	printTable(fields, rows);
	cout<<endl;
}

void showAvailableActors(TCPClientPtr client)
{
	FPAnswerPtr answer = client->sendQuest(FPQWriter::emptyQuest("availableActors"));
	FPAReader ar(answer);
	if (ar.status())
		cout<<"[Exception] Error code: "<<ar.wantInt("code")<<", ex: "<<ar.wantString("ex")<<endl;
	else
	{
		OBJECT availableActors = ar.getObject("availableActors");
		showStatusObject(availableActors, "Available actors:");
		OBJECT deployedActors = ar.getObject("deployedActors");
		showStatusObject(deployedActors, "Deployed actors:");
	}
}

void showActorTaskStatus(TCPClientPtr client)
{
	FPAnswerPtr answer = client->sendQuest(FPQWriter::emptyQuest("actorTaskStatus"));
	FPAReader ar(answer);
	if (ar.status())
		cout<<"[Exception] Error code: "<<ar.wantInt("code")<<", ex: "<<ar.wantString("ex")<<endl;
	else
	{
		std::vector<std::string> fields = ar.want("fields", std::vector<std::string>());
		std::vector<std::vector<std::string>> rows = ar.want("rows", std::vector<std::vector<std::string>>());
		formatMTime(fields, rows);
		cout<<"Actor Task Status:"<<endl;
		printTable(fields, rows);
	}
}

int main(int argc, const char* argv[])
{
	if (argc < 2 || argc > 3)
		return showUsage(argv[0]);

	std::string endpoint = argv[1];
	if (argc == 3)
		endpoint.append(":").append(argv[2]);

	TCPClientPtr client = TCPClient::createClient(endpoint);
	if (!client)
		return showUsage(argv[0]);

	showAvailableActors(client);
	showActorTaskStatus(client);

	return 0;
}