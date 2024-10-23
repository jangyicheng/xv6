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

#define NBUCKETS 13
struct
{
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  // struct buf head;
  struct buf hashbucket[NBUCKETS]; // 每个哈希队列一个linked list及一个lock
} bcache;

struct spinlock global_lock;

int hash(uint blockno)
{
  return blockno % NBUCKETS;
}

char bname[NBUCKETS][12];
void binit(void)
{
  struct buf *b;

  // Create linked list of buffers
  for (int i = 0; i < NBUCKETS; i++)
  {
    snprintf(bname[i], sizeof(bname[i]), "bcache-%d", i);
    initlock(&bcache.lock[i], bname[i]);

    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  } // 初始化bcache

  int bucket;
  for (int i = 0; i < NBUF; i++)
  {
    b = &bcache.buf[i];
    bucket = hash(b->blockno);
    b->next = bcache.hashbucket[bucket].next;
    b->prev = &bcache.hashbucket[bucket];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[bucket].next->prev = b;
    bcache.hashbucket[bucket].next = b;
  }
}
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  int bucket = hash(blockno);
  acquire(&bcache.lock[bucket]);

  // Is the block already cached?
  // 在当前桶内搜索buffer
  for (b = bcache.hashbucket[bucket].next; b != &bcache.hashbucket[bucket]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for (b = bcache.hashbucket[bucket].prev; b != &bcache.hashbucket[bucket]; b = b->prev)
  {
    if (b->refcnt == 0) // 找到一个空闲的buffer
    {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[bucket]);
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // 在其他桶内搜索buffer

  acquire(&global_lock);
  for (int i = 1; i < NBUCKETS; i++)
  {
    int id = (i + bucket) % NBUCKETS;
    acquire(&bcache.lock[id]);
    for (b = bcache.hashbucket[id].prev; b != &bcache.hashbucket[id]; b = b->prev)
    {

      if (b->refcnt == 0) // 偷取到一个空闲的buffer
      {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        //先移除
        b->next->prev = b->prev;
        b->prev->next = b->next;

        //再加入
        acquire(&bcache.lock[bucket]);
        b->next = bcache.hashbucket[bucket].next;
        b->prev = &bcache.hashbucket[bucket];

        bcache.hashbucket[bucket].next->prev = b;
        bcache.hashbucket[bucket].next = b;

        release(&bcache.lock[bucket]);
        release(&bcache.lock[id]);
        release(&global_lock);

        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[id]);
  }


  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
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
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket = hash(b->blockno);
  acquire(&bcache.lock[bucket]); // 打算释放对应的buffer，首先获取对应桶的锁
  b->refcnt--;

  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[bucket].next;
    b->prev = &bcache.hashbucket[bucket];
    bcache.hashbucket[bucket].next->prev = b;
    bcache.hashbucket[bucket].next = b;
  }

  release(&bcache.lock[bucket]);
}

void bpin(struct buf *b)
{
  int bucket = hash(b->blockno);
  acquire(&bcache.lock[bucket]);
  b->refcnt++;
  release(&bcache.lock[bucket]);
}

void bunpin(struct buf *b)
{
  int bucket = hash(b->blockno);
  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  release(&bcache.lock[bucket]);
}
