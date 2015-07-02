#!/bin/bash

javac=$HOME/mercurial/jdk8/build/linux-x86_64-normal-server-release/jdk/bin/javac
coordinator=$HOME/git/JMigration/Coordinator/dist/Debug/GNU-Linux-x86/coordinator
bin=$HOME/bin

mkdir $bin > /dev/null

cp java $bin
cp criu-dump $bin
cp criu-restore $bin
ln -s $javac $bin/javac
ln -s $coordinator $bin/coordinator

