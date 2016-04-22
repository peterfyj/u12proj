# 项目进展 #

本周实际进展不多，主要是发现了若干问题。


# 若干问题 #

  1. Sleep:会调用Select，并通过超时来实现实际的计时，需要改为ucore中的sleep；
  1. Clone:对于peter.go，发现其线程机制和goroutines的数目有关，有待进一步确定；
  1. Futex:仿照Darwin的实现，使用semaphore实现，却造成了ucore的无端重启；