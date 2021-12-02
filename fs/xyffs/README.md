# xyffs文件系统

本系统实现的功能有：
-	[x] mount
-	[x] umount
-	[x] touch
-	[x] mkdir
-	[x] cd
<br>

 - **`mount`: 挂载xyffs文件系统**

    编译后以如下命令执行xyffs文件：
    
    `./xyffs --device={ddriverpath} -f -d -s {mntpath}`
    
    其中`{ddriverpath}`是`ddriver`驱动的路径，`{mntpath}`是挂载点路径。

-	**`umount`：卸载xyffs文件系统**

    执行如下命令：
    
    `fusermount -u {mntpath}`

-	**`ls`：列出目录下的所有目录项**
    
    `ls`
    
    列出当前目录下的所有目录项
    
    `ls {path}`
    
    列出指定路径{path}下的所有目录项

-	**`mkdir`：新建目录**
    
    `mkdir {dirpath}`
    
    `{dirpath}`路径是一个目录，创建`{dirpath}`指向的目录，要求被创建目录之前的路径存在

-	**`cd`：更改当前工作目录**
    
    `cd {dirpath}`
    
    `{dirpath}`路径是一个目录，切换当前工作目录到`{dirpath}`所指向目录

-	**`touch`：新建文件**
    
    `touch {filepath}`
    
    `{filepath}`路径是一个文件，在`{filepath}`指向处创建一个新文件
