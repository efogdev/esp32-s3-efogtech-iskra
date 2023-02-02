#!/usr/bin/zsh

while [ ! -d /dev/serial ]
do
  sleep 0.2
done

/opt/esp-idf/tools/idf.py -p `ls /dev/serial/by-id/*` monitor --no-reset

