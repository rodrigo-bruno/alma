# ALMA - GC-aware JVM Live Migration

[Check our paper at Middleware'16](http://www.gsd.inesc-id.pt/~rbruno/publications/rbruno-middleware16.pdf)

How to test ALMA:

1 - compile and install our version of CRIU in your system;

2 - download the openjdk 8 source code and replace the hotspot code with the one provided in https://github.com/rodrigo-bruno/ALMA-openjdk-8-hotspot;

3 - compile and install it in your system;

4 - compile the Coordinator and the Agent components (inside this repository);

5 - repeat the previous steps in the destination site;

6 - if your application produces some output, it has to use a shared directory (so that both the source and destination sites can access output files);

7 - start the application (at the source site). The JVM needs to be using the compiled agent and the G1 GC. Example: java ... -agentpath:/path/to/agent -XX:+UseG1GC ...

8 - start the image-cache and CRIU restore at the destination site:

criu image-cache -vvv -o /tmp/image-cache.log --port 9996 < /dev/null &

criu restore -D /tmp/dump -d -vvv -o /tmp/restore.log --remote && echo "OK"

9 - start the image-proxy and the coordinator at the source site:

criu image-proxy -vvv -o /tmp/image-proxy.log --port 9996 --address 193.136.128.89 < /dev/null &

coordinator localhost 9999

ALMA can be used to migrate JVMs between two hosting system-VMs but can also be used (for testing and debugging) to migrate JVMs inside a single hosting system-VM. Before compiling our CRIU verion check the constant LOCAL_DEVEL declated in include/image-remote.h. Set it to 1 if you plan to test or debug and 0 otherwise.
