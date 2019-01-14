#!/usr/bin/python
import sys
from socket import *
import ssl,pprint

if (len(sys.argv) != 3):
    print("Usage: {} ip port".format(sys.argv[0]))
    sys.exit(1)

host = sys.argv[1] 
port = eval(sys.argv[2]) 
bufsize = 102400
addr = (host,port)


tcpClient = socket(AF_INET, SOCK_STREAM)
sslClient = ssl.wrap_socket(tcpClient, ca_certs='cert.pem')#, cert_reqs=ssl.CERT_REQUIRED) #, ca_certs='certs.pem', cert_reqs=ssl.CERT_REQUIRED) 


#fd = open("msg.txt", "rt")
#msgSend = fd.read()
#fd.close()

msgSend = 'hello world 123456'

try:
    sslClient.connect(addr)
    print(repr(sslClient.getpeername()))
    pprint.pprint(sslClient.getpeercert())
    print(sslClient.cipher())
    print(sslClient.context.get_ca_certs())
    print(sslClient.context.get_ciphers())
    #print(sslClient.getpeercert(True))
    #print(pprint.pformat(sslClient.getpeercert(True)))
    #ssl._ssl._test_decode_cert('cert.pem')

    print("send: " + msgSend)
    sslClient.write(msgSend.encode('utf8'))
    sslClient.settimeout(2) 
    while True: 
        data=sslClient.read()
        if ((not data) or (len(data) == 0)):
            break;
        print('recv: ' +  data.decode('utf8'))
except Exception as e:
    print("Exception: " + str(e))

finally:
    sslClient.close();
    print("Socket closed.")
