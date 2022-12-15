#define O_RDONLY    0x000       // 读取
#define O_WRONLY    0x001       // 写入
#define O_RDWR      0x002       // 读取和写入
#define O_CREATE    0x200       // 如果文件不存在就创建
#define O_TRUNC     0x400       // 将文件截断为0长度
#define O_NOFOLLOW  0x004       // 不允许符号链接

// 此文件用于指示打开文件后对文件的操作，用作open系统调用的第二个参数
