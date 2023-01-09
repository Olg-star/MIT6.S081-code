// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUC 13
#define BUFMAP_HASH(dev, blockno) ((((dev) << 27) | (blockno)) % NBUC)
struct
{
    struct buf buf[NBUF];//���еĻ����
    struct buf bufHash[NBUC]; // ��ԭ���������Ϊ��ϣͰ
    struct spinlock spin_array[NBUC];//Ϊÿһ��Ͱ����һ����
    struct spinlock eviction_lock;//��֤���𻺴��ط�����ԭ���ԣ���ֹͬһ����̷������黺��
} bcache;


void binit(void)
{

    for (int i = 0; i < NBUC; i++)
    {
        initlock(&(bcache.spin_array[i]), "bcache_hash");
        bcache.bufHash[i].next = 0;
    }

    for (int i = 0; i < NBUF; i++)
    { // ��ʼ�����д��̿飬������ȫ���ŵ�bufHash[0]��Ͱ��
        struct buf *b = &bcache.buf[i];
        initsleeplock(&b->lock, "buffer");
        b->lastuse = 0;
        b->refcnt = 0;
        b->valid = 0;
        b->disk = 0;
        b->blockno = 0;
        b->dev = 0;
        b->next = bcache.bufHash[0].next;
        bcache.bufHash[0].next = b;
    }
    initlock(&bcache.eviction_lock,"bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
    struct buf *b;
    uint key = BUFMAP_HASH(dev, blockno);
    acquire(&bcache.spin_array[key]); // ����keyͰ������������Ͱ��û�л���

    // Is the block already cached?
    for (b = bcache.bufHash[key].next; b; b = b->next)//���ﲻ�ü���eviction_lock,��Ϊͬ��blockno����ͬһ�黺��
    { // ��һ��ѭ�������Ƿ񱻻���
        if (b->dev == dev && b->blockno == blockno)
        { // ����ҵ���ֱ�ӷ���
            b->refcnt++;
            b->lastuse = ticks;
            release(&bcache.spin_array[key]);
            acquiresleep(&b->lock);
            return b;
        }
    }
    release(&bcache.spin_array[key]); // �ͷ�Ͱ�����������������cpu1ռ��key1��������Ͱ2������cpu2ռ��key2��������Ͱ1����

    acquire(&bcache.eviction_lock);
    //���ϸ�����Ŀ�ģ�ʹ���𻺴�ͷ��仺���Ϊ���̣߳���������cpuͬʱΪһ��blockno���뻺�浼�µ�һ�����������黺��Ľ����

    //����������ٴμ����������ڣ�������cpuͬʱΪһ��blockno���뻺��ʱ��cpu1��������eviction_lock���ȷ�����һ�黺�棬
    //��cpu2�Ͳ����������ˣ����Ե�cpu2���eviction_lock�󣬿���ͨ���ٴμ��keyͰ����û�и�blockno�Ļ��棬��Ȼ���еģ�����ֱ�ӷ��أ��������·���
    for (b = bcache.bufHash[key].next; b; b = b->next)
    // �������ڳ��� eviction_lock��û���κ������߳��ܹ������������������
    // û���κ������߳��ܹ��ı� bufmap[key] Ͱ����Ľṹ���������ﲻ���Ȼ�ȡ
    { 
        if (b->dev == dev && b->blockno == blockno)
        { // ����ҵ���ֱ�ӷ���
            acquire(&bcache.spin_array[key]);
            b->refcnt++;
            b->lastuse = ticks;
            release(&bcache.spin_array[key]);
            release(&bcache.eviction_lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    // Not cached.���û�л���
    // Recycle the least recently used (LRU) unused buffer.
    struct buf *hold_buf = 0;    // Ҫ�����buf
    uint hold_key = -1;           // Ҫ�����buf��Ͱ��key
    for (int i = 0; i < NBUC; i++)
    { // ������������Ͱ������������û�õ�
        int found = 0;
        acquire(&bcache.spin_array[i]); // ��ȡiͰ����
        for (b = &bcache.bufHash[i]; b->next; b = b->next)//ע����ֹ������b->next
        { // ������Ͱ�ϵ�����
            if (b->next->refcnt == 0 && ( !hold_buf || b->next->lastuse < hold_buf->next->lastuse))
            { // �����bufû�б��ã�ͬʱ����Ŀǰ���δʹ�õģ�������
                hold_buf = b;
                found = 1;
            }
        }
        if (!found)
        {                                   // ������iͰû���ҵ��ʺϵĻ���
            release(&bcache.spin_array[i]); // �ͷŸ�Ͱ����
        }
        else
        {
            if (hold_key != -1)
            {
                release(&bcache.spin_array[hold_key]);
            }
            hold_key = i; // ��������iͰ��������֤û��������cpu�������Ͱ��Ҫ��Ȼ���ܻᵼ��ͬһ����������������cpu,Ҳ�ᵼ�¸�Ͱ����ṹ�ı䣬�Ӷ�����
        }
    }
    if (!hold_buf)
    { // û�п��õ�buf
        panic("bget: no buffers!");
    }
    else
    { // ����о�������b��ӵ�keyͰ
        b = hold_buf->next;
        hold_buf->next = hold_buf->next->next;
        release(&bcache.spin_array[hold_key]);
    }
    acquire(&bcache.spin_array[key]);
    b->next = bcache.bufHash[key].next;
    bcache.bufHash[key].next = b;
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    release(&bcache.spin_array[key]);
    release(&bcache.eviction_lock);
    acquiresleep(&b->lock);

    return b;
}

// Return a locked buf with the contents of the indicated block.
// һ��bread��ȡ�˴������ݣ������Ҫ�Ļ����������������ظ����ĵ����ߣ������߾Ͷ�ռ��buffer�����Զ�ȡ��д�����ݡ�
struct buf *
bread(uint dev, uint blockno)
{
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid)
    {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
// ����������޸���buffer�����������ͷ�buffer֮ǰ����bwrite���޸ĺ������д����̡�bwrite����virtio_disk_rw�����Ӳ��������
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
// �������ߴ�����һ��buffer�󣬱������brelse���ͷ�����
// �ͷ�sleep-lock��������buffer�ƶ��������ͷ��
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);
    uint dev = b->dev;
    uint blockno = b->blockno;
    uint key = BUFMAP_HASH(dev,blockno);
    acquire(&bcache.spin_array[key]);
    b->refcnt--;
    if(b->refcnt<0){
        panic("b->recnt<0\n");
    }
    if (b->refcnt == 0)
    {
        //�������ʹ�õ�ʱ��
        b->lastuse = ticks;
    }

    release(&bcache.spin_array[key]);
}

void bpin(struct buf *b)
{
    uint dev = b->dev;
    uint blockno = b->blockno;
    uint key = BUFMAP_HASH(dev,blockno);
    acquire(&bcache.spin_array[key]);
    b->refcnt++;
    release(&bcache.spin_array[key]);
}

void bunpin(struct buf *b)
{
    uint dev = b->dev;
    uint blockno = b->blockno;
    uint key = BUFMAP_HASH(dev,blockno);
    acquire(&bcache.spin_array[key]);
    b->refcnt--;
    release(&bcache.spin_array[key]);
}
