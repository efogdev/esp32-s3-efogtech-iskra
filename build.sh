#!/usr/bin/zsh

[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"

cd main/ui
nvm use 16
npm install
npm run build
node post_process_build.js
cd ../..

cd devdep
node bumpVersion.js
cd ..

cp main/ui-bundle/* main
cp main/ui/upload.html main
cp main/ui/build/bundle.js main
cp main/ui/pwa.js main/sw.js

$IDF_PATH/tools/idf.py build
cp build/efogtech_iskra.bin .

if [ "$1" = "flash" ]; then

  while [ ! -d /dev/serial ]
  do
    sleep 0.2
  done

  $IDF_PATH/tools/idf.py -p `ls /dev/serial/by-id/*` flash

  if [ "$2" = "monitor" ]; then
   $IDF_PATH/tools/idf.py -p `ls /dev/serial/by-id/*` monitor
  fi

fi

if [ "$1" = "usb" ]; then
  adb push efogtech_iskra.bin /storage/emulated/0/Download
fi

