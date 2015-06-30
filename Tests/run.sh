#!/bin/bash


# Usage: ./run.sh <directory with Main.java>

echo "Building agent..."
cd ~/git/JMigration/JMigration 
make
cd -
echo "Done."
echo
echo

~/bin/javac $1/Main.java
~/bin/java -cp $1 Main
