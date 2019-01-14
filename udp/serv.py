#!/usr/bin/python
import sys
from socket import *

if (len(sys.argv) != 2):
    print("Usage: {} port".format(sys.argv[0]))
    sys.exit(1)

host = '' 
port = eval(sys.argv[1]) 
bufsize = 1024*9 - 32
addr = (host,port)

udpServer = socket(AF_INET, SOCK_DGRAM)
udpServer.bind(addr)
n = 0
try:
    while True:
        print('Waiting for message...')
        data,addr = udpServer.recvfrom(bufsize) 
        print('Recv ', data)
        n += 1
        udpServer.sendto("No.={} msg={}".format(n, data), addr)
        print('Reply to:',addr)
finally:
    udpServer.close()
    print("Socket closed.")
