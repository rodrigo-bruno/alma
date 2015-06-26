#!/bin/bash


# Usage: ./run.sh <directory with Main.java>

~/bin/javac $1/Main.java
~/bin/java -cp $1 Main
