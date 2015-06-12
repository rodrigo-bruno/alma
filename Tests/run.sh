#!/bin/bash

agent_path=/home/underscore/git/JMigration/JMigration/dist/Debug/GNU-Linux-x86/libJMigration.so

javac $1/Main.java
java -cp $1 -agentpath:$agent_path Main
