# 交叉编译环境的最大修改集及交叉编译方法 #

## 最大修改集 ##

.<br>
├──　cmd/<br>
│  　├──　8l/<br>
│  　│  　├──　asm.c：让Hucore类型与Hlinux一致（ELF32式）<br>
│  　│  　├──　doc.go：仅含注释修改，可忽略；<br>
│  　│  　├──　obj.c：在headers中添加Hucore，并使之处理与Hlinux一致；<br>
│  　│  　└──　pass.c：让Hucore类型与Hlinux一致；<br>
│  　├──　cov/<br>
│  　│  　└──　Makefile：让install-ucore按install-default处理；<br>
│  　├──　ld/<br>
│  　│  　└──　lib.h：Hucore在这里定义，为headers枚举的一种；<br>
│  　└──　prof/<br>
│  　　　　　└──　Makefile：让install-ucore按install-default处理；<br>
├──　Make.inc:将ucore加入GOOS_LIST；<br>
└──　pkg/<br>
├──　<del>crypto/</del><br>
│  　└──　<del>rand/</del><br>
│  　　　　　└──　<del>Makefile：加密包所用随机数产生器，将ucore的产生方式导向rand_unix.go即可；</del><br>
├──　<del>debug/</del><br>
│  　└──　<del>proc/</del><br>
│  　　　　　├──　<del>proc_ucore.go：提供了线程调试的平台无关接口，重构方法待定；</del><br>
│  　　　　　└──　<del>regs_ucore_386.go：提供了调试时寄存器的平台无关接口，重构方法待定；</del><br>
├──　<del>exec/</del><br>
│  　└──　<del>Makefile：将ucore导向lp_unix.go，这是外部调用，仅含目录查找；</del><br>
├──　<del>net/</del><br>
│  　├──　<del>fd_ucore.go：网络相关，将禁用；</del><br>
│  　└──　<del>Makefile：导向fd_ucore.go；</del><br>
├──　<del>os/</del><br>
│  　├──　<del>dir_ucore.go</del><br>
│  　├──　<del>inotify/</del><br>
│  　│  　├──　<del>inotify_ucore.go</del><br>
│  　│  　├──　<del>inotify_ucore_test.go</del><br>
│  　│  　└──　<del>Makefile</del><br>
│  　├──　<del>Makefile</del><br>
│  　├──　<del>stat_ucore.go</del><br>
│  　└──　<del>sys_ucore.go</del><br>
├──　<del>path/</del><br>
│  　└──　<del>filepath/</del><br>
│  　　　　　└──　<del>Makefile：导向path_unix.go，用于指定文件路径的解析方式；</del><br>
├──　runtime/<br>
│  　├──　cgo/<br>
│  　│  　└──　ucore_386.c<br>
│  　└──　ucore/<br>
│  　　　　　├──　386/<br>
│  　　　　　│  　├──　defs.h<br>
│  　　　　　│  　├──　rt0.s<br>
│  　　　　　│  　├──　signal.c<br>
│  　　　　　│  　└──　sys.s<br>
│  　　　　　├──　defs1.c<br>
│  　　　　　├──　defs2.c<br>
│  　　　　　├──　defs_arm.c<br>
│  　　　　　├──　defs.c<br>
│  　　　　　├──　mem.c<br>
│  　　　　　├──　os.h<br>
│  　　　　　├──　signals.h<br>
│  　　　　　└──　thread.c<br>
├──　syscall/ --------------------初期可以忽略这个的实现，而从runtime入手，将借鉴GOOS=tiny；--------------------<br>
│  　├──　asm_ucore_386.s：系统调用派发；<br>
│  　├──　Makefile：指定为ucore相关；<br>
│  　├──　syscall_ucore_386.go：作为普通的GO文件，对于linux，这个文件还用于产生z文件，因此它应当与z文件吻合；<br>
│  　├──　syscall_ucore.go：同上；<br>
│  　├──　zerrors_ucore_386.go：错误号；<br>
│  　├──　zsyscall_ucore_386.go：目前来看和syscall_ucore.go同样需要，差别有待进一步确定；<br>
│  　├──　zsysnum_ucore_386.go：系统调用号；<br>
│  　└──　ztypes_ucore_386.go：类型；<br>
└──　<del>time/</del><br>
└──　<del>Makefile：世界时相关，直接导向zoneinfo_unix.go；</del><br>

<h2>交叉编译方法</h2>

初始状态下进入跟目录，运行如下命令可以进行总体编译：<br>
<pre><code>./demo.sh<br>
</code></pre>

为了获得更多帮助，可以运行：<br>
<pre><code>./demo.sh --help<br>
</code></pre>

目前的编译阶段包含以下步骤：<br>
<ol><li>以Linux为环境完全编译Go，如需跳过此环节则使用-ng参数；<br>
</li><li>以ucore为目标部分重编译Go的组件，以便生成ucore为目标的编译资源，如需跳过此环节则使用-nre参数；<br>
</li><li>生成测试用二进制码并拷贝至ucore的disk0中，如需跳过此环节则使用-ntest参数；<br>
</li><li>编译运行ucore；</li></ol>

举例来说，如果现在只对ucore进行了改动，则只需运行：<br>
<pre><code>./demo.sh -ng -nre -ntest<br>
</code></pre>