struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf? 用于判断缓冲区内容是否交给磁盘。避免没写入磁盘就被覆盖
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

