# CommandMuxer
Utility to insert command in the data stream of a modem
It uses +++ (with the appropriate delays) to go back to command mode
User has to send ATO before switching back to data mode sendign a CR
The process start in "DATA" mode, just type in a COMMAND,
the MODEM will be switched to COMMAND mode using 2s-pause +++ 1s-pause
then the command will be sent ; end the COMMAND session sending ATO
followed by an empty line
During COMMAND mode, data received from pppd are simply not read,
they will be read again when going back to DATA mode
You might need some sort of sudo sysctl fs.protected_symlinks=0
to allow pppd to run from /tmp/modem that is a symlink to the actual pty
