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

struct {
  struct spinlock lock[FSBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[FSBUCKETS];

  // maintain last time new block was cached in the bucket
  uint64 newreq[FSBUCKETS];
} bcache;

static uint64 tick;

void
binit(void)
{
  struct buf *b;
  struct spinlock *lk;
  uint i;

  for(lk = bcache.lock; lk < bcache.lock + FSBUCKETS; lk++)
    initlock(lk, "bcache.bucket");

  tick = 0;

  // Create linked list of buffers
  for(i = 0; i < FSBUCKETS; i++)
    bcache.head[i].prev = bcache.head[i].next = &bcache.head[i];
  for(b = bcache.buf, i = 0; b < bcache.buf+NBUF; b++, i = (i + 1) % FSBUCKETS){
    b->next = bcache.head[i].next;
    b->prev = &bcache.head[i];
    initsleeplock(&b->lock, "buffer");
    bcache.head[i].next->prev = b;
    bcache.head[i].next = b;
  }
}

// Redistribute the no ref bufs among all buckets
// release all the bucket locks before entering
// returns back with only passed bucket's lock acquired
// return non-zero if no buffer is available
uint
redis(uint bucket)
{
  struct spinlock *lk;
  uint status;
  struct buf *b;

  for(lk = bcache.lock; lk < bcache.lock + FSBUCKETS; lk++){
    acquire(lk);

    if(lk == bcache.lock + bucket){
      // check if its needed, if not return early
      // this is due to slight duration when the bucket lock was released
      // two bgets from same bucket may be waiting for redis
      // but no need to do expensive redistribution two times

      for(b = bcache.head[bucket].prev; b != &bcache.head[bucket]; b = b->prev){
        if(b->refcnt == 0){
          status = 0;
          goto done;
        }
      }
    }
  }
  lk--;

  // redistribution
  // maintain:
  // > for each buffer, last time it was referenced
  // > for each bucket, last time it was requested for new buffer
  // logic:
  // 1. create array of bucket indices (0 to FSBUCKETS - 1)
  //    create another array of same size to store max(LRU buffer's time, bucket's time)
  //    create final array stroing the pointer to LRU buffer for each bucket
  //    -> bucket with no LRU defaults LRU buffer's time to 0 and pointer 0
  // 2. sort descending based on stored times
  // 3. two pointers, one for each type (`b` for with buffer, `nb` for no buffer). iterate (go to next of its type)
  //    IF `b` time < `nb` time => give LRU buffer of `b` to `nb` and advance both `b` and `nb`
  //    ELSE => advance `b`
  //    ENDS as soon as any of `b` or `nb` reaches end
  uint buckets[FSBUCKETS];
  uint64 times[FSBUCKETS];
  int _b, _nb;
  struct buf *lru_buffers[FSBUCKETS], *h;

  status = 0;
  for(_b = 0; _b < FSBUCKETS; _b++){
    buckets[_b] = _b;

    times[_b] = 0;
    lru_buffers[_b] = 0;
    for(b = bcache.head[_b].prev; b != &bcache.head[_b]; b = b->prev){
      if(b->refcnt == 0)
        times[_b] = b->lastref, lru_buffers[_b] = b, status++;
    }

    if(bcache.newreq[_b] > times[_b])
      times[_b] = bcache.newreq[_b];
  }
  // return early if no buffers are left
  status = !status;
  if(status)
    goto done;

  for(_b = 1; _b < FSBUCKETS; _b++){
    for(_nb = _b - 1; _nb >= 0 && times[buckets[_nb]] < times[buckets[_nb + 1]]; _nb--){
      buckets[_nb] ^= buckets[_nb + 1];
      buckets[_nb + 1] ^= buckets[_nb];
      buckets[_nb] ^= buckets[_nb + 1];
    }
  }

  for(_b = _nb = -1; _b < FSBUCKETS && _nb < FSBUCKETS; ){
    if(_b == _nb){
      while(++_nb < FSBUCKETS && lru_buffers[buckets[_nb]]) ;
    } else if(times[buckets[_b]] < times[buckets[_nb]]){
      b = lru_buffers[buckets[_b]];
      h = &bcache.head[buckets[_nb]];

      b->prev->next = b->next;
      b->next->prev = b->prev;
      b->next = h;
      b->prev = h->prev;
      h->prev->next = b;
      h->prev = b;

      while(++_nb < FSBUCKETS && lru_buffers[buckets[_nb]]) ;
    }

    while(++_b < FSBUCKETS && !lru_buffers[buckets[_b]]) ;
  }

done:
  for(; lk >= bcache.lock; lk--){
    if(lk != bcache.lock + bucket)
      release(lk);
  }

  return status;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint bucket = blockno % FSBUCKETS;

  acquire(&bcache.lock[bucket]);

fetch:
  // Is the block already cached?
  for(b = bcache.head[bucket].next; b != &bcache.head[bucket]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->lastref = ++tick;
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  bcache.newreq[bucket] = ++tick;
  for(b = bcache.head[bucket].prev; b != &bcache.head[bucket]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Need to redistribute, all buffers are busy for the bucket
  release(&bcache.lock[bucket]);
  if(redis(bucket))
    panic("bget: no buffers");
  goto fetch;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint bucket = b->blockno % FSBUCKETS;

  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[bucket].next;
    b->prev = &bcache.head[bucket];
    bcache.head[bucket].next->prev = b;
    bcache.head[bucket].next = b;
  }

  release(&bcache.lock[bucket]);
}

void
bpin(struct buf *b) {
  uint bucket = b->blockno % FSBUCKETS;

  acquire(&bcache.lock[bucket]);
  b->refcnt++;
  release(&bcache.lock[bucket]);
}

void
bunpin(struct buf *b) {
  uint bucket = b->blockno % FSBUCKETS;

  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  release(&bcache.lock[bucket]);
}


