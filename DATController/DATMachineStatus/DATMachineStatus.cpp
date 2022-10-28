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

int findIndex(const std::string& field, const std::vector<std::string>& fields)
{
	for (size_t i = 0; i < fields.size(); i++)
		if (fields[i] == field)
			return (int)i;

	return -1;
}

void formatStatus(std::vector<std::string>& fields, std::vector<std::vector<std::string>>& rows)
{
	int cpuIdx = findIndex("cpus", fields);
	int loadIdx = findIndex("load", fields);
	int memIdx = findIndex("memories", fields);
	int freeMemIdx = findIndex("freeMemories", fields);
	int rxIdx = findIndex("RX", fields);
	int txIdx = findIndex("TX", fields);

	fields.push_back("load/cpus");

	for (auto& row: rows)
	{
		int cpus = atoi(row[cpuIdx].c_str());
		double load = atof(row[loadIdx].c_str());

		if (cpus)
			row.push_back(std::to_string(load/cpus));
		else
			row.push_back("N/A");

		row[memIdx] = formatBytesQuantity(atoll(row[memIdx].c_str()), 2);
		row[freeMemIdx] = formatBytesQuantity(atoll(row[freeMemIdx].c_str()), 2);
		row[rxIdx] = formatBytesQuantity(atoll(row[rxIdx].c_str()), 2);
		row[txIdx] = formatBytesQuantity(atoll(row[txIdx].c_str()), 2);
	}
}

void showMachineStatus(TCPClientPtr client)
{
	FPAnswerPtr answer = client->sendQuest(FPQWriter::emptyQuest("machineStatus"));
	FPAReader ar(answer);
	if (ar.status())
		cout<<"[Exception] Error code: "<<ar.wantInt("code")<<", ex: "<<ar.wantString("ex")<<endl;
	else
	{
		std::vector<std::string> fields = ar.want("fields", std::vector<std::string>());
		std::vector<std::vector<std::string>> rows = ar.want("rows", std::vector<std::vector<std::string>>());

		formatStatus(fields, rows);
		printTable(fields, rows);
	}
}

bool openMonitor(TCPClientPtr client)
{
	FPQWriter qw(1, "monitorMachineStatus");
	qw.param("monitor", true);

	FPAnswerPtr answer = client->sendQuest(qw.take());
	FPAReader ar(answer);
	if (ar.status())
	{
		cout<<"[Exception] Error code: "<<ar.wantInt("code")<<", ex: "<<ar.wantString("ex")<<endl;
		return false;
	}
	return true;
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

	if (!openMonitor(client))
		return 0;

	while (true)
	{
		showMachineStatus(client);
		cout<<endl;
		sleep(2);
	}

	return 0;
}