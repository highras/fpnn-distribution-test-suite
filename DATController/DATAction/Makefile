FPNN_DIR = ../../../infra-fpnn
DEPLOYMENT_DIR = ../../../deployment/rpm

CFLAGS +=
CXXFLAGS +=
CPPFLAGS += -std=c++11 -I$(FPNN_DIR)/base -I$(FPNN_DIR)/proto -I$(FPNN_DIR)/core -I$(FPNN_DIR)/proto/msgpack -I$(FPNN_DIR)/proto/rapidjson
LIBS += -L$(FPNN_DIR)/extends -L$(FPNN_DIR)/core -L$(FPNN_DIR)/proto -L$(FPNN_DIR)/base -lfpnn

EXES_SERVER = DATAction
EXES_CLIENT = DATActionAll

OBJS_SERVER = DATAction.o
OBJS_CLIENT = DATActionAll.o


all: $(EXES_SERVER) $(EXES_CLIENT)

clean:
	$(RM) $(EXES_SERVER) $(EXES_CLIENT) *.o

include $(FPNN_DIR)/def.mk