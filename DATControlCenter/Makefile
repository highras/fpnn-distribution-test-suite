FPNN_DIR = ../../infra-fpnn
DEPLOYMENT_DIR = ../../deployment/rpm

CFLAGS +=
CXXFLAGS +=
CPPFLAGS += -std=c++11 -I$(FPNN_DIR)/base -I$(FPNN_DIR)/proto -I$(FPNN_DIR)/core -I$(FPNN_DIR)/proto/msgpack -I$(FPNN_DIR)/proto/rapidjson
LIBS += -L$(FPNN_DIR)/extends -L$(FPNN_DIR)/core -L$(FPNN_DIR)/proto -L$(FPNN_DIR)/base -lfpnn

EXES_SERVER = DATControlCenter

OBJS_SERVER = DATControlCenter.o ControlCenterQuestProcessor.o


all: $(EXES_SERVER)

clean:
	$(RM) $(EXES_SERVER) *.o

include $(FPNN_DIR)/def.mk