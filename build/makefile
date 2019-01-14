all : ssl tcp udp copyTarget

#curPath = ${shell pwd}
curPath = ${shell dirname ${realpath ${lastword ${MAKEFILE_LIST}}}}
CPPFLAGS +=  -I${curPath}/../include

destDir = ../bin
copyTarget: 
	cp ../ssl/cli.py ${destDir}/sslcli.py
	cp ../ssl/serv.py ${destDir}/sslserv.py
	cp ../tcp/cli.py ${destDir}/tcpcli.py
	cp ../tcp/serv.py ${destDir}/tcpserv.py
	cp ../udp/cli.py ${destDir}/udpcli.py
	cp ../udp/serv.py ${destDir}/udpserv.py
	cp sslclient ${destDir}/
	cp sslserver ${destDir}/
	cp tcpclient ${destDir}/
	cp tcpserver ${destDir}/
	cp udpclient ${destDir}/
	cp udpserver ${destDir}/

CC = gcc
ssl : sslclient sslserver
tcp : tcpclient tcpserver
udp : udpclient udpserver

vpath %.h ../include 
vpath %.cpp ../ssl ../tcp ../udp

sslclient : sslclient.o
	$(CC) -o $@ $< -lssl -lcrypto
sslserver : sslserver.o
	$(CC) -o $@ $< -lssl -lcrypto
tcpclient : tcpclient.o
tcpserver : tcpserver.o
udpclient : udpclient.o
udpserver : udpserver.o

*.o : myerr.h

.PHONY : clean
clean :
	-rm sslclient sslserver 
	-rm tcpclient tcpserver
	-rm udpclient udpserver
	-rm *.o
