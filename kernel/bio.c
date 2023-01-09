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
    struct buf buf[NBUF];//所有的缓存块
    struct buf bufHash[NBUC]; // 将原来的链表改为哈希桶
    struct spinlock spin_array[NBUC];//为每一个桶分配一个锁
    struct spinlock eviction_lock;//保证驱逐缓存重分配是原子性，防止同一块磁盘分了两块缓存
} bcache;


void binit(void)
{

    for (int i = 0; i < NBUC; i++)
    {
        initlock(&(bcache.spin_array[i]), "bcache_hash");
        bcache.bufHash[i].next = 0;
    }

    for (int i = 0; i < NBUF; i++)
    { // 初始化所有磁盘块，并将它全部放到bufHash[0]的桶里
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
    acquire(&bcache.spin_array[key]); // 持有key桶的锁，来检查该桶有没有缓存

    // Is the block already cached?
    for (b = bcache.bufHash[key].next; b; b = b->next)//这里不用加上eviction_lock,因为同个blockno返回同一块缓存
    { // 第一次循环检查块是否被缓存
        if (b->dev == dev && b->blockno == blockno)
        { // 如果找到了直接返回
            b->refcnt++;
            b->lastuse = ticks;
            release(&bcache.spin_array[key]);
            acquiresleep(&b->lock);
            return b;
        }
    }
    release(&bcache.spin_array[key]); // 释放桶的锁，否则会死锁。cpu1占着key1的锁申请桶2的锁，cpu2占着key2的锁申请桶1的锁

    acquire(&bcache.eviction_lock);
    //加上该锁的目的：使驱逐缓存和分配缓存成为单线程，以免两块cpu同时为一个blockno申请缓存导致的一个磁盘有两块缓存的结果。

    //接下来这个再次检查的意义在于：当两块cpu同时为一个blockno申请缓存时，cpu1如果获得了eviction_lock后先分配了一块缓存，
    //那cpu2就不再用申请了，所以当cpu2获得eviction_lock后，可以通过再次检查key桶里有没有该blockno的缓存，必然是有的，所以直接返回，不再重新分配
    for (b = bcache.bufHash[key].next; b; b = b->next)
    // 这里由于持有 eviction_lock，没有任何其他线程能够进行驱逐操作，所以
    // 没有任何其他线程能够改变 bufmap[key] 桶链表的结构，所以这里不事先获取
    { 
        if (b->dev == dev && b->blockno == blockno)
        { // 如果找到了直接返回
            acquire(&bcache.spin_array[key]);
            b->refcnt++;
            b->lastuse = ticks;
            release(&bcache.spin_array[key]);
            release(&bcache.eviction_lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    // Not cached.如果没有缓存
    // Recycle the least recently used (LRU) unused buffer.
    struct buf *hold_buf = 0;    // 要驱逐的buf
    uint hold_key = -1;           // 要驱逐的buf的桶的key
    for (int i = 0; i < NBUC; i++)
    { // 遍历查找所有桶，查找最近最久没用的
        int found = 0;
        acquire(&bcache.spin_array[i]); // 获取i桶的锁
        for (b = &bcache.bufHash[i]; b->next; b = b->next)//注意终止条件是b->next
        { // 遍历该桶上的链表
            if (b->next->refcnt == 0 && ( !hold_buf || b->next->lastuse < hold_buf->next->lastuse))
            { // 如果该buf没有被用，同时又是目前最久未使用的，则标记它
                hold_buf = b;
                found = 1;
            }
        }
        if (!found)
        {                                   // 就是在i桶没有找到适合的缓存
            release(&bcache.spin_array[i]); // 释放该桶的锁
        }
        else
        {
            if (hold_key != -1)
            {
                release(&bcache.spin_array[hold_key]);
            }
            hold_key = i; // 继续持有i桶的锁，保证没有其他的cpu来动这个桶，要不然可能会导致同一个缓存分配给了两个cpu,也会导致该桶链表结构改变，从而报错
        }
    }
    if (!hold_buf)
    { // 没有可用的buf
        panic("bget: no buffers!");
    }
    else
    { // 如果有就来驱逐b添加到key桶
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
// 一旦bread读取了磁盘内容（如果需要的话）并将缓冲区返回给它的调用者，调用者就独占该buffer，可以读取或写入数据。
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
// 如果调用者修改了buffer，它必须在释放buffer之前调用bwrite将修改后的数据写入磁盘。bwrite调用virtio_disk_rw与磁盘硬件交互。
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
// 当调用者处理完一个buffer后，必须调用brelse来释放它。
// 释放sleep-lock，并将该buffer移动到链表的头部
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
        //更新最近使用的时间
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
