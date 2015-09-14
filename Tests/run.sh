#!/bin/bash


# Usage: ./run.sh <directory with Main.java>

echo "Building agent..."
cd ~/git/JMigration/Agent
make
cd -
echo "Done."

echo "Building test..."
~/bin/javac $1/Main.java
echo "Done."

echo "Starting test..."
~/bin/java -cp $1 Main
