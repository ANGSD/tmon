[![Build Status](https://travis-ci.org/ANGSD/tmon.svg?branch=master)](https://travis-ci.org/ANGSD/tmon)

	 tMon - A distributed resource monitor

To install:

	git clone https://github.com/ANGSD/tmon
	cd tmon;make
	make install

This will first compile both the daemon and client, then copy the daemon, 
tmond, to /usr/sbin and the client, tmon, to /usr/bin. Note that only root 
should be allowed to install the binaries.

Edit and copy the tmonrc file to your home directory as ~/.tmonrc which is the
default filename. Alternative filenames may be specified on the command line. 

This code was forked from an ubuntu package by thorfinn thorfinn@binf.ku.dk back in 2007-2009.

See changelog for details of changes

It using both the /etc/init.d/tmond and /lib/systemd/system/tmond.service

For systemd do

	systemctl enable tmond.service
