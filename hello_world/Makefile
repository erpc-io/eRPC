# Notes:
# 1. The make target depends on how eRPC was compiled:
#   * If DTRANSPORT=dpdk, use `make dpdk`
#   * If DTRANSPORT=raw, use `make raw`
#   * If DTRANSPORT=infiniband, use `make infiniband`
error:
	@echo "Please choose one of the following targets: infiniband, raw, dpdk, clean"
	@exit 2
infiniband:
	g++ -g -std=c++11 -o server server.cc -I ../src -L ../build -lerpc -libverbs -lpthread -lnuma -ldl -DINFINIBAND=true
	g++ -g -std=c++11 -o client client.cc -I ../src -L ../build -lerpc -libverbs -lpthread -lnuma -ldl -DINFINIBAND=true
raw:
	g++ -g -std=c++11 -o server server.cc -I ../src -L ../build -lerpc -libverbs -lpthread -lnuma -ldl -DRAW=true
	g++ -g -std=c++11 -o client client.cc -I ../src -L ../build -lerpc -libverbs -lpthread -lnuma -ldl -DRAW=true
dpdk:
	g++ -g -std=c++11 -o server server.cc -I ../src -I /usr/include/dpdk -L ../build -lerpc -lpthread -lnuma -ldpdk -ldl -DDPDK=true
	g++ -g -std=c++11 -o client client.cc -I ../src -I /usr/include/dpdk -L ../build -lerpc -lpthread -lnuma -ldpdk -ldl -DDPDK=true
clean:
	rm server client

