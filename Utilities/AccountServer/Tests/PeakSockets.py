import os
import sys
import time

"""
	Run this script via command line to see the peak number of open sockets.
	You can specify which port to use below, which defaults to 8000, for WebSrv.
	This was used in tuning WebSrv performance by configuration of Apache/WSGI.
"""

PORT = 8000

if __name__ == "__main__":
	peak = None
	command = '/bin/netstat -n -t | grep :%d | grep ESTABLISHED -c' % PORT
	while True:
		try:
			count = int(os.popen(command).read())
			if(count > peak):
				peak = count
				sys.stderr.write('   %d\r' % peak)
			time.sleep(0.1)
		except KeyboardInterrupt:
			sys.stderr.write('\n')
			sys.exit()
