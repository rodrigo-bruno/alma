#!/bin/bash


# Usage: ./run.sh <directory with Main.java>

agent_path=/home/underscore/git/JMigration/JMigration/dist/Debug/GNU-Linux-x86/libJMigration.so
openjdk=/home/underscore/mercurial/jdk8/build/linux-x86_64-normal-server-release/jdk/bin
javac=$openjdk/javac
java=$openjdk/java

$javac $1/Main.java
$java -cp $1 -XX:+UseG1GC -agentpath:$agent_path Main
