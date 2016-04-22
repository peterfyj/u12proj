# 项目进展 #

  * 上周提到的格式输出错误实际为可能发生的情况，因为Println不是原子的，本问题被忽略；
  * 实现了exit\_group，修正了一个Bug，在退出thread\_group的时候同时把组中其他proc从timer\_list中删除；
  * ldt问题彻底解决：目前ucore的gdt表中单独存在一项用于tls，每个tls存于proc\_struct中，并且在切换的时候更新到gdt中的tls里。这样，Go可以使用的m就可以到达ucore的上限了；
  * 更新工具链，使得进一步测试更为方便：目前的工具链可以自动编译所有测试样例；
  * 进行了一部分测试，目前，有关于channal/goroutines的测试已经全部通过。详情请见[TestReport](TestReport.md)；
  * 完成了ucore在gcc4.6上编译的功能。主要问题是bootblock比原来大，超过了512字节，不能放到第一扇区中，经过询问同学了解到，需要将.eh\_frame section去除。.eh\_frame是用来进行异常处理的代码，在操作系统层面上是不需要这些代码的，在gcc4.4中，默认的编译选项没有加入这个section，而在4.6中，我们需要显示地指定去除相应代码。

> 一开始，有同学建议使用strip去除.eh\_frame的section。这种方法确实能去除.eh\_frame，但是连目标文件中的所有符号也被去除了。这导致了一个问题：在切换到保护模式的时候，由于标号地址的不正确，系统跳转到了不正确的地方。因此，我们需要找到一个方法，在不去除符号的情况下安全地移除.eh\_frame。

> 我们最终使用的方法是更改linker script。linker script能够被ld调用，按照我们的意愿进行相应的链接。一开始，我们将系统默认的linker script打印出来（有几百行），尝试进行修改，但是无论怎么修改（甚至是不修改），链接出来的结果都是不正确的（远远超出了512字节）。在查阅了一些资料以后，我们尝试编写如下一个linker script：

```
OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(start)
SECTIONS {
    .text : {
        *(.text)
    }
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
    }
    /DISCARD/ : {
        *(.eh_*)
    }
}
```

保存为ucore.lds。在bootblock的ld中加入选项-Tucore.lds指定使用该linker script，生成的bootblock大小终于又变回了444B，调试编译通过。

# 备注 #

  * exit\_group和exit的区别还是挺大的。前者还需要把同一个thread\_group中的proc都清理掉。这里的清理包括了从timer\_list等中移除。exit\_group的第一个实现就是因为没有把同一个线程组中的线程都从timer\_list中除去，因此panic了。

# 问题 #

  * ucore在gcc 4.6上不能编译的问题已经折磨了我将近一周的时间。由于gcc 4.6套件链接出来的bootblock大小超过512B，导致ucore不能正常编译。根据lc和zwl两位同学的方法，将bootblock的.eh\_frame用strip命令去除可以编译通过，但是就不能正常运行ucore了。目前还没有在gcc 4.6上正常编译的方法，已知可行的方法是将gcc回滚到4.4。将继续尝试解决这一问题。(已解决)