#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <atomic>

#include "dqueue.h"
#include "osp.h"
#include "spinlock.h"

#define OSP_TRAILERBYTES        64
// #undef OSP_TRAILERBYTES

#define OSP_TRASHONFREE       1
// #undef OSP_TRASHONFREE

/* This structure must be a multiple of 16 bytes long */
class ospMemHdr {
public:
    static const uint16_t _magicFree = 0x1f1f;
    static const uint16_t _magicAlloc = 0x1a1a;

    uint16_t _magic;
    uint8_t _hdrSize;
    uint8_t _padding1;
    uint32_t _padding2;
    size_t _allocSize;        /* bytes including header */
    void *_retAddrp;
    void *_retAddr1p;
    void *_retAddr2p;
#ifdef OSP_TRAILERBYTES
    ospMemHdr *_dqNextp;
    ospMemHdr *_dqPrevp;
#endif
    void *_padding3;
};

SpinLock _ospMemLock;
dqueue<ospMemHdr> _ospMemAllocs;

void *
ospRetAddr(void *prevRetAddrp)
{
    uint32_t i;
    void **datap;
    void **framep = NULL;
    void **newFramep = NULL;

    datap = &prevRetAddrp;
    for(i=1;i<20;i++) { /* start at 1 to avoid finding our parameter */
        if (datap[i] == prevRetAddrp) {
            framep = datap+(i-1);
            break;
        }
    }

    if (framep == NULL)
        return NULL;

    /* make sure the new frame is 8 byte aligned, and somewhat (but
     * not too far) past the previous frame.  If we want to go further
     * up the stack, we can repeat the next few lines repeatedly.
     * Basically, each frame pointer points to the caller's frame
     * pointer, and the 64 bit pointer immediately following the frame
     * pointer is the saved return address.
     */
    newFramep = (void **) *framep;
    if (((uint64_t)newFramep) & 7)
        return 0;
    if ( (uint64_t)newFramep - (uint64_t)framep > 0x400)
        return 0;

    return newFramep[1];
}

void *
operator new(size_t asize)
{
    ospMemHdr *hdrp;
    size_t allocSize;

    /* malloc seems to round up to nearest 16 bytes, so let's do the same in case
     * there are undetected overflow in standard code.
     */
    asize = (asize+15) & ~15;

#ifdef OSP_TRAILERBYTES
    allocSize = asize + sizeof(ospMemHdr) + OSP_TRAILERBYTES;
#else
    allocSize = asize + sizeof(ospMemHdr);
#endif
    hdrp = (ospMemHdr *) malloc(allocSize);
    hdrp->_magic = ospMemHdr::_magicAlloc;
    hdrp->_hdrSize = sizeof(ospMemHdr);
    hdrp->_allocSize = allocSize;
    hdrp->_padding1 = 0x00;
    hdrp->_retAddrp = __builtin_return_address(0);
    hdrp->_retAddr1p = ospRetAddr(hdrp->_retAddrp);
#ifdef OSP_TRAILERBYTES
    // printf("MEM ALLOC hdrp=%p ret=%p\n", hdrp, hdrp->_retAddrp);
    _ospMemLock.take();
    _ospMemAllocs.append(hdrp);
    memset(((char *) (hdrp+1)) + asize, 0x5a, OSP_TRAILERBYTES);
    _ospMemLock.release();
#endif
    return (void *)(hdrp+1);
}

void
operator delete(void *aptr) noexcept
{
    if (!aptr) {
        return;
    }

    ospMemHdr *hdrp = (ospMemHdr *)aptr;
    hdrp--;
#ifdef OSP_TRAILERBYTES
    {
        uint32_t i;
        char *datap;
        datap = ((char *)(hdrp+1))+hdrp->_allocSize - sizeof(ospMemHdr) - OSP_TRAILERBYTES;
        _ospMemLock.take();
        for(i=0;i<OSP_TRAILERBYTES;i++, datap++) {
            assert(*datap == 0x5a);
        }
        _ospMemLock.release();
    }
#endif
    assert(hdrp->_magic == ospMemHdr::_magicAlloc);

#ifdef OSP_TRAILERBYTES
    _ospMemLock.take();
    _ospMemAllocs.remove(hdrp);
    _ospMemLock.release();
    // printf("MEM FREE hdrp=%p re=%p magic=%x delret=%p\n", hdrp, hdrp->_retAddrp, hdrp->_magic, __builtin_return_address(0));
#endif

    hdrp->_magic = ospMemHdr::_magicFree;
#ifdef OSP_TRASHONFREE
    memset(hdrp+1, 'x', hdrp->_allocSize - sizeof(ospMemHdr));
#endif
    free(hdrp);
}

void
ospMemCheck()
{
#ifdef OSP_TRAILERBYTES
    ospMemHdr *hdrp;
    char *datap;
    uint32_t i;
    _ospMemLock.take();
    for(hdrp = _ospMemAllocs.head(); hdrp; hdrp=hdrp->_dqNextp) {
        datap = ((char *)(hdrp))+hdrp->_allocSize - OSP_TRAILERBYTES;
        for(i=0;i<OSP_TRAILERBYTES;i++, datap++) {
            assert(*datap == 0x5a);
        }
    }
    _ospMemLock.release();
#endif
}
