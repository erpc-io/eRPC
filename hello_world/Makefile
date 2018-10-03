#Notes:
#1. The make target depends on how eRPC was compiled:
#   * If DTRANSPORT=dpdk, use `make dpdk`
#   * If DTRANSPORT=raw, use `make raw`
#   * If DTRANSPORT=infiniband, use `make infiniband`
LIBS = -lerpc -lpthread -lnuma -ldl

error:
	@echo "Please choose one of the following targets: infiniband, raw, dpdk, clean"
	@exit 2
infiniband:
	g++ -g -std=c++11 -o server server.cc -I ../src -L ../build $(LIBS) -libverbs -DINFINIBAND=true
	g++ -g -std=c++11 -o client client.cc -I ../src -L ../build $(LIBS) -libverbs -DINFINIBAND=true
raw:
	g++ -g -std=c++11 -o server server.cc -I ../src -L ../build $(LIBS) -libverbs -DRAW=true
	g++ -g -std=c++11 -o client client.cc -I ../src -L ../build $(LIBS) -libverbs -DRAW=true
dpdk:
	g++ -g -std=c++11 -o server server.cc -I ../src -I /usr/local/include/dpdk -L ../build $(LIBS) -ldpdk -DDPDK=true -march=native
	g++ -g -std=c++11 -o client client.cc -I ../src -I /usr/local/include/dpdk -L ../build $(LIBS) -ldpdk -DDPDK=true -march=native
clean:
	rm server client

