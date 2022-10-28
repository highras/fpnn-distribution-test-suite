#include <iostream>
#include <unistd.h>
#include <sys/sysinfo.h>
#include "ServerInfo.h"
#include "ignoreSignals.h"
#include "TCPClient.h"
#include "MonitorQuestProcessor.h"

using namespace std;
using namespace fpnn;

class Monitor
{
	TCPClientPtr _client;
	std::string _region;

public:
	bool init(int argc, const char* argv[])
	{
		if (argc < 2 || argc > 3)
			return false;

		std::string endpoint = argv[1];
		if (argc == 3)
			endpoint.append(":").append(argv[2]);

		_region = ServerInfo::getServerRegionName();
		_client = TCPClient::createClient(endpoint);
		if (!_client)
			return false;

		_client->setQuestProcessor(std::make_shared<MonitorQuestProcessor>());
		return true;
	}

	void loop()
	{
		while (true)
		{
			if (!_client->connected())
			{
				_client->connect();
				registerMonitor();
			}
			sleep(2);
		}
		
	}
	void registerMonitor()
	{
		struct sysinfo info;
		sysinfo(&info);

		FPQWriter qw(3, "registerMonitor");
		qw.param("region", _region);
		qw.param("cpus", get_nprocs());
		qw.param("totalMemories", info.totalram);

		TCPClientPtr client = _client;
		_client->sendQuest(qw.take(), [client](FPAnswerPtr answer, int errorCode){
			if (errorCode != FPNN_EC_OK)
			{
				cout<<"[Error] Register monitor self exception. error code: "<<errorCode<<endl;
				client->close();
			}
		});
	}
};	

int showUsage(const char* appName)
{
	cout<<"Usgae:"<<endl;
	cout<<"\t"<<appName<<" endpoint"<<endl;
	cout<<"\t"<<appName<<" host port"<<endl;
	return -1;
}

int main(int argc, const char* argv[])
{
	ignoreSignals();
	ClientEngine::configAnswerCallbackThreadPool(1, 1, 1, 2);
	ClientEngine::configQuestProcessThreadPool(1, 1, 1, 2, 0);

	Monitor monitor;

	if (!monitor.init(argc, argv))
		return showUsage(argv[0]);

	monitor.loop();

	return 0;
}