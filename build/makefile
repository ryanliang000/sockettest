all : ssl tcp udp copyTarget

#curPath = ${shell pwd}
curPath = ${shell dirname ${realpath ${lastword ${MAKEFILE_LIST}}}}
CPPFLAGS +=  -I${curPath}/../include
OBJS += $(notdir $(patsubst %.cpp, %.o, $(wildcard ../ssl/*.cpp)))
OBJS += $(notdir $(patsubst %.cpp, %.o, $(wildcard ../tcp/*.cpp)))
OBJS += $(notdir $(patsubst %.cpp, %.o, $(wildcard ../udp/*.cpp)))
Target = $(patsubst %.o, %, $(OBJS))
vpath %.h ../include 
vpath %.cpp ../ssl ../tcp ../udp

destDir = $(curPath)/../bin
copyTarget: 
	$(foreach f, ssl tcp udp, $(shell cp ../$(f)/cli.py $(destDir)/$(f)cli.py)) 
	$(foreach f, ssl tcp udp, $(shell cp ../$(f)/serv.py $(destDir)/$(f)serv.py))
	$(foreach n, ${Target}, ${shell cp ${n} ${destDir}})

CC = gcc
ssl : sslclient sslserver
tcp : tcpclient tcpserver
udp : udpclient udpserver

LDLIBS += -lssl -lcrypto
sslclient : sslclient.o
sslserver : sslserver.o
	$(CC) -o $@ $< -lssl -lcrypto
	@echo ${OBJS}
tcpclient : tcpclient.o
tcpserver : tcpserver.o
udpclient : udpclient.o
udpserver : udpserver.o

$(OBJS) : myerr.h

.PHONY : clean
clean :
	-rm $(Target) $(OBJS) 
