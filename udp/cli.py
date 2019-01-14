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

udpClient = socket(AF_INET, SOCK_DGRAM)
try:
    while True:
        msg = raw_input("> ")
        if (not msg):
            break;
        
        udpClient.sendto(msg, addr)
        
        data, raddr=udpClient.recvfrom(bufsize)
        if (data is None):
            break;
        print('recv: ' +  data)
finally:
    udpClient.close();
    print("Socket closed.")
