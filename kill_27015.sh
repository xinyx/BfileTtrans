taskkill -F -PID `netstat -nao | findstr 27015 | awk -e '{print $5}'`
