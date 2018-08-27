# Notes:
# 1. Use `make dpdk` if you complied eRPC with DTRANSPORT=dpdk, else use
#   `make verbs`.
#
# 2. This assumes that DPDK is installed system-wide and the DPDK headers are
#    available in `/usr/include/dpdk/`

verbs:
	g++ -g -std=c++11 -march=native -o server server.cc -I ../src -L ../build -lerpc -libverbs -lpthread -lnuma -ldpdk -ldl
	g++ -g -std=c++11 -march=native -o client client.cc -I ../src -L ../build -lerpc -libverbs -lpthread -lnuma -ldpdk -ldl
dpdk:
	g++ -g -std=c++11 -march=native -o server server.cc -I ../src -I /usr/include/dpdk -DDPDK -L ../build -lerpc -lpthread -lnuma -ldpdk -ldl
	g++ -g -std=c++11 -march=native -o client client.cc -I ../src -I /usr/include/dpdk -DDPDK -L ../build -lerpc -lpthread -lnuma -ldpdk -ldl
clean:
	rm server client

