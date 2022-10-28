#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include "ignoreSignals.h"
#include "CommandLineUtil.h"
#include "TCPClient.h"
#include "CtrlQuestProcessor.h"

using namespace std;
using namespace fpnn;

int main(int argc, const char* argv[])
{
	ignoreSignals();
	ClientEngine::configAnswerCallbackThreadPool(2, 1, 2, 4);
	ClientEngine::configQuestProcessThreadPool(0, 1, 2, 10, 0);

	CommandLineParser::init(argc, argv);
	std::string endpoint = CommandLineParser::getString("h");

	TCPClientPtr client = TCPClient::createClient(endpoint);
	if (!client)
	{
		cout<<"Usage: ./DATPrototypeController -h endpoint"<<endl;
		return -1;
	}

	client->setQuestProcessor(std::make_shared<CtrlQuestProcessor>());
	client->connect();

	//-- check actor
	//-- upload actor
	//-- deploy actor
	//-- do test
	//-- parse result

	sleep(1);
	cout<<"Demo all done."<<endl;

	return 0;
}