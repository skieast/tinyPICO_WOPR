# W.O.P.R. Display

This code is extracted and slightly modified from the original [UnexpectedMaker code](https://github.com/UnexpectedMaker/wopr)   
to be compiled with Platformio. 

Some other changes are eliminating `secret.h` and pull the WIFI_PASS and WIFI_SSID from environment variables during compile.
This is only tested on Windows using Powerwhell but should work with Linux or MacOS. I leave that as an exercise for the reader.

Pull in wifi password and ssid from environment variables (you may need some escaping, no idea here)
```
 WIFI_PASS
 WIFI_SSID
 Windows Powershell  	
    $env:WIFI_PASS = 'Some password'
    $env:WIFI_SSID = 'Some ssid'

 Notes for Windows CMD shell
	The characters <, >, |, &, and ^ are special command shell characters, 
	and they must be preceded by the escape character (^) or enclosed in quotation marks
	when used in <string> (for example, "StringContaining&Symbol"). 
	If you use quotation marks to enclose a string that contains one of the special characters, 
	the quotation marks are set as part of the environment variable value.
 Windows CMD			WIFI_PASS=Some super password
 Fish shell 			set -x WIFI_PASS "Super Fish password"
 Bash shell			export WIFI_PASS="Even better WIFI password"
```

# Support Unexpected Maker

That said, a lot of time, effort and finances have gone into designing and releasing these files, so please consider supporting him by buy some of his TinyPICO products:

https://tinypico.com/shop

Or by buying one of his products on tindie:

https://www.tindie.com/stores/seonr/

Or by becoming a Patron:

https://www.patreon.com/unexpectedmaker


# Unexpected Maker
http://youtube.com/c/unexpectedmaker

http://twitter.com/unexpectedmaker

https://www.facebook.com/unexpectedmaker/

https://www.instagram.com/unexpectedmaker/
