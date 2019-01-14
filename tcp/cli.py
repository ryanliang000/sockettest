#!/usr/bin/python
import sys
from socket import *

if (len(sys.argv) != 3):
    print("Usage: {} ip port".format(sys.argv[0]))
    sys.exit(1)

host = sys.argv[1] 
port = eval(sys.argv[2]) 
bufsize = 1024
addr = (host,port)

tcpClient = socket(AF_INET, SOCK_STREAM)
try:
    tcpClient.connect(addr)
    while True:
        msg = raw_input("> ")
        if (not msg):
            break;
        
        tcpClient.send(msg)
        
        data=tcpClient.recv(bufsize)
        if (data is None):
            break;
        print('recv: ' +  data)
finally:
    tcpClient.close();
    print("Socket closed.")
