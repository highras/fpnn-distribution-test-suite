#ifndef DAT_Deployer_Quest_Processor_h
#define DAT_Deployer_Quest_Processor_h

#include "IQuestProcessor.h"

using namespace fpnn;

struct UploadStatus
{
	int fd;
	int sectionLength;
	std::string name;
	std::string tmpFilePath;
	std::map<int, std::string> sections;
	bool token;
	int64_t activeSecs;

	UploadStatus(const std::string& name, int sectionCount, const std::string& cachePath);
	~UploadStatus();

	void fetchSectionToWrite(int& no, std::string& section, bool& allDone);
};
typedef std::shared_ptr<UploadStatus> UploadStatusPtr;

struct UploadInfo
{
	std::mutex _mutex;
	std::map<int, UploadStatusPtr> _status;

	void wroteUploadSections(int taskId);
	void fillUploadSection(int taskId, const std::string& name, const std::string& cachePath, int sectionCount, int sectionNo, std::string& content);
	void checkUploadTimeout();
};
typedef std::shared_ptr<UploadInfo> UploadInfoPtr;

void updateActorInfos(const std::string& name, const std::string& tmpPath);

class DeployQuestProcessor: public IQuestProcessor
{
	QuestProcessorClassPrivateFields(DeployQuestProcessor)

	std::string _cachePath;
	std::string _tmpFileCachePath;
	UploadInfoPtr _uploadInfos;

	void prepareCachePath(const std::string& cachePath);

public:
	DeployQuestProcessor(const std::string& cachePath)
	{
		prepareCachePath(cachePath);

		registerMethod("deployActor", &DeployQuestProcessor::deployActor);
		registerMethod("systemCmd", &DeployQuestProcessor::systemCmd);
		registerMethod("launchActor", &DeployQuestProcessor::launchActor);
		registerMethod("machineStatus", &DeployQuestProcessor::machineStatus);
		registerMethod("ping", &DeployQuestProcessor::ping);

		_uploadInfos.reset(new UploadInfo());
	}

	FPAnswerPtr deployActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr systemCmd(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr launchActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr machineStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci);
	FPAnswerPtr ping(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
	{
		return FPAWriter::emptyAnswer(quest);
	}

	std::string cachePath() { return _cachePath; }
	void checkUploadTimeout() { _uploadInfos->checkUploadTimeout(); }

	QuestProcessorClassBasicPublicFuncs
};

#endif