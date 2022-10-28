#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <chrono>
#include "ignoreSignals.h"
#include "CommandLineUtil.h"
#include "TCPClient.h"

using namespace std;
using namespace fpnn;

std::mutex gc_mutex;
std::condition_variable gc_condition;
int gc_action = 0;

void showUsage(const char* appName)
{
	cout<<"Usgae:"<<endl;
	cout<<"\t"<<appName<<" -e endpoint --actor actor-name --region region-name"<<endl;
	cout<<"\t"<<appName<<" -h host -p port --actor actor-name --region region-name"<<endl;

	cout<<"\t"<<appName<<" -e endpoint --actor actor-name --endpoints target-endpoints"<<endl;
	cout<<"\t"<<appName<<" -h host -p port --actor actor-name --endpoints target-endpoints"<<endl;
}

int findFieldIndex(const std::string& field, const std::vector<std::string>& fields)
{
	for (int i = 0; i < (int)fields.size(); i++)
		if (fields[i] == field)
			return i;

	return -1;
}

class CtrlQuestProcessor: public IQuestProcessor
{
	QuestProcessorClassPrivateFields(CtrlQuestProcessor)

public:
	CtrlQuestProcessor()
	{
		registerMethod("deployFinish", &CtrlQuestProcessor::deployFinish);
	}

	FPAnswerPtr deployFinish(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
	{
		sendAnswer(FPAWriter::emptyAnswer(quest));
		
		int taskId = args->wantInt("taskId");
		std::vector<std::string> failedEndpoints = args->get("failedEndpoints", std::vector<std::string>());

		{
			std::unique_lock<std::mutex> lck(gc_mutex);

			if (failedEndpoints.empty())
				cout<<endl<<"Deploy successed. Task Id: "<<taskId<<endl;
			else
			{
				cout<<endl<<"Deploy finish:"<<endl;
				cout<<"Task Id: "<<taskId<<endl;
				cout<<failedEndpoints.size()<<" endpoint(s) failed."<<endl<<"Failed endpoint(s):";
				for (std::string& ep: failedEndpoints)
					cout<<" "<<ep;
				cout<<endl;
			}

			gc_action += 1;
			if (gc_action == 1)
				gc_condition.notify_one();
		}

		return nullptr;
	}

	virtual void connectionWillClose(const ConnectionInfo& connInfo, bool closeByError)
	{
		std::unique_lock<std::mutex> lck(gc_mutex);
		gc_action += 1;
		if (gc_action == 1)
		{
			cout<<endl<<"Deploy status unknown. Connection is closed."<<endl;
			gc_condition.notify_one();
		}
	}

	QuestProcessorClassBasicPublicFuncs
};

class DeployExecutor
{
	TCPClientPtr _client;
	std::string _actor;
	std::string _region;
	std::set<std::string> _eps;
	bool _useRegion;

	void prepare(const char* appName);
	void fetchTargetEndpoints();
	bool checkActorUploadStatus();
	void waitServerEnsure();
	std::string fetchCenterServerCachedMd5(OBJECT& availableActors);
	void filterTargetEndpoints(OBJECT& deployedActors, const std::string& targetMd5);

public:
	DeployExecutor(int argc, const char** argv);
	int deploy();
};

DeployExecutor::DeployExecutor(int argc, const char** argv)
{
	CommandLineParser::init(argc, argv);
	prepare(argv[0]);
}

void DeployExecutor::prepare(const char* appName)
{
	std::string endpoint = CommandLineParser::getString("e");
	if (endpoint.empty())
	{
		endpoint = CommandLineParser::getString("h");
		string port = CommandLineParser::getString("p");
		if (endpoint.empty() || port.empty())
			return showUsage(appName);

		endpoint.append(":").append(port);
	}

	_actor = CommandLineParser::getString("actor");
	if (_actor.empty())
		return showUsage(appName);

	_useRegion = CommandLineParser::exist("region");
	if (_useRegion)
		_region = CommandLineParser::getString("region");

	fetchTargetEndpoints();
	if (!_useRegion && _eps.empty())
		return showUsage(appName);

	_client = TCPClient::createClient(endpoint);
	if (!_client)
		return;

	_client->setQuestProcessor(std::make_shared<CtrlQuestProcessor>());
}

void DeployExecutor::fetchTargetEndpoints()
{
	if (!CommandLineParser::exist("endpoints"))
		return;

	//-- Deal this case: --endpoints eps1, eps2, eps3,eps4,eps5 eps6 eps7
	std::vector<std::string> tmp = CommandLineParser::getRestParams();
	tmp.push_back(CommandLineParser::getString("endpoints"));

	std::string all = StringUtil::join(tmp, ",");
	StringUtil::split(all, ",", _eps);
}

int DeployExecutor::deploy()
{
	if (!_client)
	{
		cout<<"Invalid endpoint or host and port."<<endl;
		return -1;
	}

	if (!checkActorUploadStatus())
		return -1;

	if (_eps.empty())
	{
		cout<<"No server need to deploy. All are latest."<<endl;
		return 0;
	}

	FPQWriter qw(2, "deploy");
	qw.param("endpoints", _eps);
	qw.param("actor", _actor);

	FPAnswerPtr answer = _client->sendQuest(qw.take());
	if (!answer)
	{
		cout<<"Send deploy quest failed."<<endl;
		return -1;
	}

	{
		FPAReader ar(answer);
		std::unique_lock<std::mutex> lck(gc_mutex);
		if (ar.status())
		{
			cout<<"[Exception] Error code: "<<ar.wantInt("code")<<", ex: "<<ar.wantString("ex")<<endl;
			return -1;
		}
		else
			cout<<"Wait deploy ensure. Task id: "<<ar.wantInt("taskId")<<endl;
	}

	waitServerEnsure();
	return 0;
}

std::string DeployExecutor::fetchCenterServerCachedMd5(OBJECT& availableActors)
{
	FPReader ar(availableActors);
	std::vector<std::string> fields = ar.want("fields", std::vector<std::string>());
	std::vector<std::vector<std::string>> rows = ar.want("rows", std::vector<std::vector<std::string>>());

	int nameIdx = findFieldIndex("name", fields);
	int md5Idx = findFieldIndex("md5", fields);

	if (nameIdx < 0 || md5Idx < 0)
		return "";

	for (auto& row: rows)
		if (row[nameIdx] == _actor)
			return row[md5Idx];

	return "";
}

void DeployExecutor::filterTargetEndpoints(OBJECT& deployedActors, const std::string& targetMd5)
{
	FPReader ar(deployedActors);
	std::vector<std::string> fields = ar.want("fields", std::vector<std::string>());
	std::vector<std::vector<std::string>> rows = ar.want("rows", std::vector<std::vector<std::string>>());

	int regionIdx = findFieldIndex("region", fields);
	int endpointIdx = findFieldIndex("endpoint", fields);
	int nameIdx = findFieldIndex("actorName", fields);
	int md5Idx = findFieldIndex("md5", fields);

	if (_useRegion)
		for (auto& row: rows)
			if (row[regionIdx] == _region)
				_eps.insert(row[endpointIdx]);

	for (auto& row: rows)
		if (row[nameIdx] == _actor && row[md5Idx] == targetMd5)
			_eps.erase(row[endpointIdx]);
}

bool DeployExecutor::checkActorUploadStatus()
{
	FPAnswerPtr answer = _client->sendQuest(FPQWriter::emptyQuest("availableActors"));
	FPAReader ar(answer);
	if (ar.status())
	{
		cout<<"[Exception][Check Actor Upload Status] Error code: "<<ar.wantInt("code")<<", ex: "<<ar.wantString("ex")<<endl;
		return false;
	}
	
	OBJECT availableActors = ar.getObject("availableActors");
	std::string deployedMd5 = fetchCenterServerCachedMd5(availableActors);
	if (deployedMd5.empty())
	{
		cout<<"[Error] Actor "<<_actor<<" haven't been cached on center server. Please upload it."<<endl;
		return false;
	}

	OBJECT deployedActors = ar.getObject("deployedActors");
	filterTargetEndpoints(deployedActors, deployedMd5);
	return true;
}

void DeployExecutor::waitServerEnsure()
{
	std::unique_lock<std::mutex> lck(gc_mutex);
	while (gc_action != 1)
		if (gc_condition.wait_for(lck, std::chrono::seconds(20)) == std::cv_status::timeout)
		{
			cout<<"."<<flush;
			_client->sendQuest(FPQWriter::emptyQuest("ping"), [](FPAnswerPtr answer, int errorCode){
				if (errorCode != FPNN_EC_OK)
				{
					std::unique_lock<std::mutex> lck(gc_mutex);
					cout<<"Ping DAT Control Center failed. Erro code "<<errorCode<<"."<<endl;
				}
			}, 0);
		}
}

int main(int argc, const char** argv)
{
	ignoreSignals();
	ClientEngine::configAnswerCallbackThreadPool(2, 1, 2, 4);
	ClientEngine::configQuestProcessThreadPool(0, 1, 2, 2, 0);

	DeployExecutor executor(argc, argv);
	return executor.deploy();
}