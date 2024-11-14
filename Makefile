# C++ compiler flags - XXX config.gmk sets this already, so APPEND ONLY XXX
CXXFLAGS += -g -O3 -std=c++11 -fdiagnostics-color=always

ToolDAQPath=/HOME/ToolAnalysis/ToolDAQApplication/ToolDAQ

ZMQInclude= -I $(ToolDAQPath)/zeromq-4.0.7/include
ZMQLib= -L $(ToolDAQPath)/zeromq-4.0.7/lib -lzmq 

all: router dealer

router: minimal_zmq_router.cpp errnoname.c
	g++ $(CXXFLAGS) $^ -I ./ $(ZMQInclude) $(ZMQLib) -o $@

dealer: minimal_zmq_dealer.cpp errnoname.c
	g++ $(CXXFLAGS) $^ -I ./ $(ZMQInclude) $(ZMQLib) -o $@
