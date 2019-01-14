#!/usr/bin/python
import sys
from socket import *
from time import ctime
import ssl
if (len(sys.argv) != 2):
    print("Usage: {} port".format(sys.argv[0]))
    sys.exit(1)

host = '' 
port = eval(sys.argv[1]) 
bufsize = 10240
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
        try:
            sslClient = ssl.wrap_socket(tcpClient, 
                server_side=True,
                certfile='./certs/cert.pem',
                keyfile='./certs/key.pem',
                ssl_version=ssl.PROTOCOL_TLSv1_2)
        except Exception, e:
            print("Ssl Exception: " + str(e))
            continue;

        print("After ssl wrap socket")
        while True:
            data = sslClient.read()
            if (not data):
                break;
            print('Recv :', data)
            sslClient.write('[{}]'.format(ctime()).encode('utf8'))
            sslClient.write(data)
        
        sslClient.close()
finally:
    tcpServer.close()
    print("Socket closed.")
