all:
	g++ -g -std=c++11 -o server server.cc -I ../src -I /usr/include/dpdk -L ../build -lerpc -libverbs -lpthread -lnuma -ldpdk
	g++ -g -std=c++11 -o client client.cc -I ../src -I /usr/include/dpdk -L ../build -lerpc -libverbs -lpthread -lnuma -ldpdk
clean:
	rm server client

