#!/usr/bin/python
import sys
from socket import *
from time import ctime
if (len(sys.argv) != 2):
    print("Usage: {} port".format(sys.argv[0]))
    sys.exit(1)

host = '192.168.0.102'
port = eval(sys.argv[1]) 
bufsize = 1024
addr = (host,port)

tcpServer = socket(AF_INET, SOCK_STREAM)
tcpServer.bind(addr)
tcpServer.listen(5)  #5-max links limit
n = 0
try:
    while True:
        print('Waiting for connection...')
        tcpClient, addr = tcpServer.accept()
        n+=1
        print('No.={}, connect from '.format(n), addr)

        while True:
            data = tcpClient.recv(bufsize)
            if (not data):
                break;
            print('Recv :', data)
            tcpClient.send('[{}] {}'.format(ctime(), data))
        tcpClient.close()

finally:
    tcpServer.close()
    print("Socket closed.")
