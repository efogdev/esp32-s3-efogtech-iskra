#!/usr/bin/zsh

export NVM_DIR="$([ -z "${XDG_CONFIG_HOME-}" ] && printf %s "${HOME}/.nvm" || printf %s "${XDG_CONFIG_HOME}/nvm")"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh" # This loads nvm

cd main/ui
nvm use 16
npm install
npm run build
node post_process_build.js
cd ../..
cp main/ui-bundle/* main
cp main/ui/build/bundle.js main

/opt/esp-idf/tools/idf.py build
cp build/efogtech_iskra.bin .

#while [ ! -d /dev/serial ]
#do
#  sleep 0.2
#done

/opt/esp-idf/tools/idf.py -p `ls /dev/serial/by-id/*` flash monitor --no-reset
