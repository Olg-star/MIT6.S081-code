struct buf {
  int valid;   // has data been read from disk? ��ʾ�Ƿ�����ÿ�ĸ������Ƿ�Ӵ��̶�ȡ�����ݣ�
  int disk;    // does disk "own" buf? ��ʾ�������������Ѿ����޸���Ҫ������д����̡�
  uint lastuse; // ���ʹ�õ�ʱ��
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

