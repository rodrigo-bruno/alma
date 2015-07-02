#!/bin/bash


# Usage: ./run.sh <directory with Main.java>

CRIU_SERVICE_ADDR="/var/run/criu-service.socket"
CRIU_PID_FILE="/var/run/criu-service.pid"

echo "Building agent..."
cd ~/git/JMigration/Agent
make
cd -
echo "Done."

echo "Building test..."
~/bin/javac $1/Main.java
echo "Done."

echo "Starting service..."
sudo kill $(sudo cat $CRIU_PID_FILE) 
sudo rm -f $CRIU_PID_FILE
sudo rm $CRIU_SERVICE_ADDR
sudo criu service -v4 -o service.log --address $CRIU_SERVICE_ADDR -d --pidfile $CRIU_PID_FILE -W . || { echo "FAIL service start"; exit 1; }
echo "Done."                                                         

echo "Starting test..."
~/bin/java -cp $1 Main
