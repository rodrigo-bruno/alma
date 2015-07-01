#!/bin/bash


# Usage: ./run.sh <directory with Main.java>

echo "Building agent..."
cd ~/git/JMigration/JMigration 
make
cd -
echo "Done."

echo "Building test..."
~/bin/javac $1/Main.java
echo "Done."

echo "Starting service..." 
rm -f pidfile
criu service -v4 -o service.log --address /tmp/criu-service -d --pidfile pidfile -W . || { echo "FAIL service start"; exit 1; }
echo "Done."                                                         

echo "Starting test..."
~/bin/java -cp $1 Main
