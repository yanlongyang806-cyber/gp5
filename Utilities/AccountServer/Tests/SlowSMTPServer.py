#!/usr/bin/env python

"""
Like the built-in debugging server, but responds after a long time.
This simulates a slow-to-respond mail server. Inspired by:
http://muffinresearch.co.uk/archives/2010/10/15/fake-smtp-server-with-python/
"""

SLEEP_TIME = 10
SMTP_PORT = 1025

import smtpd
import time
import asyncore

class SlowSMTPServer(smtpd.SMTPServer):
	def __init__(*args, **kwargs):
		print "Running a fake, slow smtp server on port: %d" % SMTP_PORT
		smtpd.SMTPServer.__init__(*args, **kwargs)

	def process_message(self, peer, mailfrom, rcpttos, data):
		print "Sleeping for %d seconds..." % SLEEP_TIME
		time.sleep(SLEEP_TIME)

if __name__ == "__main__":
	smtp_server = SlowSMTPServer(('localhost', SMTP_PORT), None)
	try:
		asyncore.loop()
	except KeyboardInterrupt:
		smtp_server.close()
