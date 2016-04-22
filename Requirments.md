# Typical Requirments #

  * changing the linker to know how to generate the appropriate binary format (probably an ELF variant).
  * implementing the assembly in src/pkg/runtime to make system calls to allocate memory, set up a thread local storage segment, print output, exit, find the environment, and maybe a few other things.
  * implementing the code to generate the z files in package syscall for your operating system.
  * implementing the OS-specific support in os, time, and net, usually on top of what's in syscall.

# Acknowledgement #

  * Special thanks to Russ for these details.