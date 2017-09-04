all:
	g++ -g -std=c++14 -o server server.cc -I ../../src -L ../../build -lerpc -libverbs -lpthread -lenet -lnuma
	g++ -g -std=c++14 -o client client.cc -I ../../src -L ../../build -lerpc -libverbs -lpthread -lenet -lnuma
clean:
	rm server client

