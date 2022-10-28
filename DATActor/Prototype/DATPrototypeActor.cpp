#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include "ServerInfo.h"
#include "ignoreSignals.h"
#include "CommandLineUtil.h"
#include "TCPClient.h"
#include "ActorQuestProcessor.h"

using namespace std;
using namespace fpnn;

class Actor
{
	std::string _region;
	TCPClientPtr _client;

public:
	bool init(const std::string& endpoint)
	{
		_region = ServerInfo::getServerRegionName();
		_client = TCPClient::createClient(endpoint);
		if (!_client)
			return false;

		_client->setQuestProcessor(std::make_shared<ActorQuestProcessor>());
		return true;
	}

	void registerActor();
	void check()
	{
		if (!_client->connected())
		{
			_client->connect();
			registerActor();
		}
	}
};

void Actor::registerActor()
{
	FPQWriter qw(3, "registerActor");
	qw.param("region", _region);
	qw.param("name", "Prototype Actor");
	qw.param("pid", (int64_t)getpid());

	TCPClientPtr client = _client;
	_client->sendQuest(qw.take(), [client](FPAnswerPtr answer, int errorCode){
		if (errorCode != FPNN_EC_OK)
		{
			cout<<"[Error] Register prototype actor exception. error code: "<<errorCode<<endl;
			client->close();
		}
	});
}

int main(int argc, const char* argv[])
{
	ignoreSignals();
	ClientEngine::configAnswerCallbackThreadPool(2, 1, 2, 4);
	ClientEngine::configQuestProcessThreadPool(0, 1, 2, 10, 0);

	CommandLineParser::init(argc, argv);
	std::string endpoint = CommandLineParser::getString("h");

	Actor actor;
	if (!actor.init(endpoint))
	{
		cout<<"Usage: ./DATPrototypeActor -h endpoint"<<endl;
		return -1;
	}

	while (true)
	{
		actor.check();
		sleep(2);
	}

	return 0;
}