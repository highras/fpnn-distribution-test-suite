#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <chrono>
#include "ignoreSignals.h"
#include "FileSystemUtil.h"
#include "TCPClient.h"

using namespace std;
using namespace fpnn;

const size_t gc_maxTransportLength = 2 * 1024 * 1024;

std::mutex gc_mutex;
std::condition_variable gc_condition;
int action = 0;
bool uploadOk = false;

class UploadActorCallback
{
	std::mutex _mutex;
	std::set<int> _taskId;
	std::set<int> _failedSections;
public:
	~UploadActorCallback()
	{
		std::unique_lock<std::mutex> lck(gc_mutex);
		cout<<endl;

		if (_taskId.size() == 1)
			cout<<"Task id: "<<(*(_taskId.begin()))<<endl;
		else
		{
			cout<<"Receive "<<_taskId.size()<<" task ids."<<endl<<"Task ids:";
			for (int id: _taskId)
				cout<<" "<<id;
			cout<<endl;
		}

		if (_failedSections.size())
		{
			cout<<_failedSections.size()<<" section(s) failed."<<endl<<"Section No.:";
			for (int no: _failedSections)
				cout<<" "<<no;
			cout<<endl;
		}

		action += 1;
		if (action == 2)
			gc_condition.notify_one();
	}

	void addTaskId(int taskId)
	{
		std::unique_lock<std::mutex> lck(_mutex);
		_taskId.insert(taskId);
	}

	void addFailedSection(int sectionNo)
	{
		std::unique_lock<std::mutex> lck(_mutex);
		_failedSections.insert(sectionNo);
	}
};

class CtrlQuestProcessor: public IQuestProcessor
{
	QuestProcessorClassPrivateFields(CtrlQuestProcessor)

public:
	CtrlQuestProcessor()
	{
		registerMethod("uploadFinish", &CtrlQuestProcessor::uploadFinish);
	}

	FPAnswerPtr uploadFinish(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
	{
		int taskId = args->wantInt("taskId");
		std::string actor = args->wantString("actor");
		uploadOk = args->wantBool("ok");

		{
			std::unique_lock<std::mutex> lck(gc_mutex);

			cout<<endl<<"Upload finish notify:"<<endl;
			cout<<"Task Id: "<<taskId<<endl;
			cout<<"Actor: "<<actor<<endl;
			cout<<"Ok: "<<(uploadOk ? "true" : "false")<<endl;

			action += 1;
			if (action == 2)
				gc_condition.notify_one();
		}

		return FPAWriter::emptyAnswer(quest);
	}

	virtual void connectionWillClose(const ConnectionInfo& connInfo, bool closeByError)
	{
		std::unique_lock<std::mutex> lck(gc_mutex);
		action += 1;
		if (action == 2)
		{
			cout<<endl<<"Upload failed. Connection is closed."<<endl;
			gc_condition.notify_one();
		}
	}

	QuestProcessorClassBasicPublicFuncs
};

bool uploadActor(TCPClientPtr client, const std::string& actorPath)
{
	std::string actorName, ext;
	if (!FileSystemUtil::getFileNameAndExt(actorPath, actorName, ext))
	{
		cout<<"Parse file name and ext with "<<actorPath<<" failed."<<endl;
		return false;
	}

	std::string content;
	if (FileSystemUtil::readFileContent(actorPath, content) == false || content.empty())
	{
		cout<<"Actor is not exist or cannot be loaded or actor invalid."<<endl;
		return false;
	}

	//-- calculate sections --//
	size_t parts = content.length() / gc_maxTransportLength;
	if (content.length() % gc_maxTransportLength)
		parts += 1;
	
	cout<<"Upload actor length "<<content.length()<<", will be transported as "<<parts<<" section(s)."<<endl;
	//-- send update quests --//
	std::shared_ptr<UploadActorCallback> allCB(new UploadActorCallback());

	size_t remain = content.length();
	size_t offset = 0;
	for (size_t i = 0; i < parts; i++)
	{
		int no = i + 1;
		FPQWriter qw(4, "uploadActor");
		qw.param("name", actorName);
		qw.paramBinary("section", content.data() + offset, (remain > gc_maxTransportLength) ? gc_maxTransportLength : remain);
		qw.param("count", parts);
		qw.param("no", no);

		bool status = client->sendQuest(qw.take(), [no, allCB](FPAnswerPtr answer, int errorCode){
			if (errorCode == FPNN_EC_OK)
			{
				FPAReader ar(answer);
				allCB->addTaskId(ar.wantInt("taskId"));
			}
			else if (errorCode != FPNN_EC_CORE_TIMEOUT)
			{
				allCB->addFailedSection(no);
				
				std::unique_lock<std::mutex> lck(gc_mutex);
				cout<<"Section "<<no<<" failed. Error code: "<<errorCode<<endl;
			}
		}, i * 5);
		if (!status)
			allCB->addFailedSection(no);

		remain -= gc_maxTransportLength;
		offset += gc_maxTransportLength;
	}

	return true;
}

int main(int argc, const char* argv[])
{
	ignoreSignals();
	ClientEngine::configAnswerCallbackThreadPool(2, 1, 2, 4);
	ClientEngine::configQuestProcessThreadPool(0, 1, 2, 2, 0);

	std::string endpoint, filePath;
	if (argc == 3)
	{
		endpoint = argv[1];
		filePath = argv[2];
	}
	else if (argc == 4)
	{
		endpoint = argv[1];
		filePath = argv[3];
		endpoint.append(":").append(argv[2]);
	}
	else
	{
		cout<<"Usage:"<<endl;
		cout<<"\t"<<argv[0]<<" endpoint actor_path"<<endl;
		cout<<"\t"<<argv[0]<<" host port actor_path"<<endl;
		return -1;
	}

	TCPClientPtr client = TCPClient::createClient(endpoint);
	if (!client)
	{
		cout<<"Invalid endpoint or host and port."<<endl;
		return -1;
	}

	client->setQuestProcessor(std::make_shared<CtrlQuestProcessor>());
	if (!uploadActor(client, filePath))
		return -1;

	std::unique_lock<std::mutex> lck(gc_mutex);
	while (action != 2)
  		//gc_condition.wait(lck);
  		if (gc_condition.wait_for(lck, std::chrono::seconds(20)) == std::cv_status::timeout)
  		{
  			cout<<"."<<flush;
  			client->sendQuest(FPQWriter::emptyQuest("ping"), [](FPAnswerPtr answer, int errorCode){
  				if (errorCode != FPNN_EC_OK)
  				{
  					std::unique_lock<std::mutex> lck(gc_mutex);
  					cout<<"Ping DAT Control Center failed. Erro code "<<errorCode<<"."<<endl;
  				}
  			}, 0);
  		}

  	if (uploadOk)
  		cout<<"Upload successful."<<endl;
  	else
  		cout<<"Upload failed."<<endl;

	return 0;
}