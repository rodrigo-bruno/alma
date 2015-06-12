#!/bin/bash

agent_path=/home/underscore/git/JMigration/JMigration/dist/Debug/GNU-Linux-x86/libJMigration.so

function T0-Load {
	javac $FUNCNAME/Main.java
	java -cp $FUNCNAME -agentpath:$agent_path Main
}

T0-Load
