#ifndef DAT_Control_Center_Quest_Processor_h
#define DAT_Control_Center_Quest_Processor_h

#include "TaskThreadPool.h"
#include "IQuestProcessor.h"

using namespace fpnn;

struct ActorInfo
{
	time_t mtime;
	size_t fileSize;
	std::string fileMd5;
	std::string desc;
};

struct DeployHost
{
	std::string region;
	std::string endpoint;

	bool operator< (const struct DeployHost& r) const
	{
		if (region < r.region)
			return true;
		else if (region == r.region)
		{
			if (endpoint < r.endpoint)
				return true;
		}
		return false;
	}

	bool operator== (const struct DeployHost& r) const
	{
		return (region == r.region && endpoint == r.endpoint);
	}
};

enum class ClientRole
{
	Controller,
	Deployer,
	Monitor,
	Actor,
};

class ControlCenterQuestProcessor;
typedef std::shared_ptr<ControlCenterQuestProcessor> ControlCenterQuestProcessorPtr;

struct UploadInfo
{
	QuestSenderPtr sender;
	std::string tmpFilePath;
	std::string name;
	std::string desc;
	int fd;
	int taskId;
	int sectionLength;
	std::map<int, std::string> sections;
	ControlCenterQuestProcessorPtr CCQP;
	bool token;

	UploadInfo(const std::string& name, const std::string& desc, int sectionCount, ControlCenterQuestProcessorPtr ccqp);
	~UploadInfo();

	void fetchSectionToWrite(int& no, std::string& section, bool& allDone);
};
typedef std::shared_ptr<struct UploadInfo> UploadInfoPtr;

struct ConnectionPrivateData
{
	std::mutex _mutex;
	UploadInfoPtr _upload;

	ClientRole _role;
	struct DeployHost _deployHost;
	std::string _actorName;
	int _actorPid;
	bool _monitoringMachineStatus;

	ConnectionPrivateData(): _role(ClientRole::Controller), _actorPid(0), _monitoringMachineStatus(false) {}

	void registerRole(ClientRole role, const std::string& region, const std::string& endpoint);
	void wroteUploadSections();
	bool fillUploadSection(const std::string& name, const std::string& desc, int sectionCount, int sectionNo,
		std::string& content, QuestSenderPtr sender, ControlCenterQuestProcessorPtr ccqp);
};
typedef std::shared_ptr<struct ConnectionPrivateData> ConnectionPrivateDataPtr;

struct ActorProcessInfo
{
	QuestSenderPtr sender;
	std::map<int, std::vector<std::string>>	taskMap;	//-- map<task id, [method, desc]>
};

struct MonitorInfo
{
	int cpuCount;
	int tcpCount;
	int udpCount;
	float systemLoad;
	int64_t delayInMsec;
	int64_t memoryCount;
	int64_t freeMemories;
	uint64_t recvBytes;
	uint64_t sendBytes;
	uint64_t recvBytesDiff;
	uint64_t sendBytesDiff;
	QuestSenderPtr sender;

	MonitorInfo(): cpuCount(0), tcpCount(0), udpCount(0), systemLoad(0.0), delayInMsec(0), memoryCount(0), freeMemories(0),
		recvBytes(0), sendBytes(0), recvBytesDiff(0), sendBytesDiff(0) {}
};

struct DeoplyerInfo: public MonitorInfo
{
	std::map<std::string, struct ActorInfo> actorInfos;
};

class ControlCenterQuestProcessor: public IQuestProcessor, public std::enable_shared_from_this<ControlCenterQuestProcessor>
{
	QuestProcessorClassPrivateFields(ControlCenterQuestProcessor)

	bool _running;
	std::mutex _mutex;
	std::mutex _fileMutex;
	std::string _cachePath;
	std::string _descFilePath;
	std::string _tmpFileCachePath;
	TaskThreadPool _taskPool;
	std::map<std::string, struct ActorInfo> _actorInfos;
	std::map<int, ConnectionPrivateDataPtr> _connData;

	std::map<struct DeployHost, struct MonitorInfo> _monitorInfos;
	std::map<struct DeployHost, struct DeoplyerInfo> _deployerInfos;
	std::map<struct DeployHost, std::map<std::string, std::map<int, struct ActorProcessInfo>>> _runningActorInfos; //-- map<host, map<actor, map<pid, info>>>

	std::map<int, std::map<int, QuestSenderPtr>> _monitorMap;	//-- map<taskId, map<socket, QuestSender>>
	std::thread _deployerMonitorThread;
	std::atomic<int> _monitorMachineStatus;

	void prepareActorCache();
	void loadActorCache();
	void persistentActorDesc();
	void deployerMontiorCycle();

	FPAnswerPtr returnActorInfos(const FPQuestPtr quest);
	void writeUploadActor(int socket, ConnectionPrivateData* idAddr);
	std::map<struct DeployHost, QuestSenderPtr> fetchDeployerSenders(const std::string& region, std::set<std::string>& ips);
	FPAnswerPtr forwardActorStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);

public:
	ControlCenterQuestProcessor();
	~ControlCenterQuestProcessor();

	//-- for controller
	FPAnswerPtr ping(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr deploy(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr uploadActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr machineStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr reloadActorInfo(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr availableActors(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr actorTaskStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr actorAction(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr systemCmd(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr launchActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr monitorTasks(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr monitorMachineStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);

	//-- for deployer
	FPAnswerPtr registerDeployer(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr registerMonitor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);

	//-- for actors
	FPAnswerPtr registerActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr actorStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr actorResult(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);

	virtual void connected(const ConnectionInfo&);
	virtual void connectionWillClose(const ConnectionInfo& connInfo, bool closeByError);

	void addNewActor(const std::string& name, const std::string& desc, const std::string& tmpPath);
	const std::string& tmpFileCachePath() { return _tmpFileCachePath; }
	void actorTaskFinish(const std::string& endpoint, const std::string& actor, int pid, int taskId);
	void adjustMachineDelay(bool deployerRole, struct DeployHost host, int64_t cost);
	void adjustMachineStatus(bool deployerRole, struct DeployHost host, int intervalSec, FPAReader& ar);
	
	QuestProcessorClassBasicPublicFuncs
};

#endif