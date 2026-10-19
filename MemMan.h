#pragma once
#include <stdlib.h>
#include <string.h>
#include <new>
#include <bit>

#ifdef MEMMAN_ENABLE_PRINT
#include <stdio.h>
#endif // MEMMAN_ENABLE_PRINT

/*

MemMan is a simple memory manager, which manually makes allocations from a
preallocated chunk of memory.

Features:
    - Works by splitting preallocated "chunks" of memory into blocks with
    allocation info embeded in memory directly before the allocation.
    - Allocations return raw pointers. There is no reference counting or handle
    system. The user expected to manually code matching alloc/free calls.
    - Designed to work by preallocating the entire pool of memory and
    allocating from that pool withing application code.
    - When preallocated pool is filled, new linked chunk will be created.
    An allocation request will always be successful even if a request is larger
    than the planned chunk expansion, in which case we fall back to
    straightforward `malloc` call.
    - Everything is always aligned and sized to universal ALIGN.
    - SmallBlocks allocation available for sizes less than ALIGN.
    - Configure using MemManConfig and MEMMAN_ENABLE_PRINT

Example:

    // make a static allocation somewhere central
    MemMan<4_MB> mem;

    ...
    mem.alloc(1024)


*/

using byte_t = unsigned char;
static constexpr size_t BitsPerByte = 8;

static constexpr size_t operator ""_MB(unsigned long long mb) {
    return (size_t)(mb * 1024 * 1024);
}

static constexpr size_t operator ""_KB(unsigned long long kb) {
    return (size_t)(kb * 1024);
}

struct MemManConfig {
    size_t SIZE = 4_MB;
    size_t SMALL_BLOCKS_SIZE = 4_KB;
    uint8_t ALIGN = 16;
    uint8_t BYTES_PER_SMALL_BLOCK = ALIGN;
};

template <MemManConfig CONFIG>
class alignas(CONFIG.ALIGN) MemMan {

    static constexpr size_t SIZE = CONFIG.SIZE;
    static constexpr size_t SMALL_BLOCKS_SIZE = CONFIG.SMALL_BLOCKS_SIZE;
    static constexpr uint8_t ALIGN = CONFIG.ALIGN;
    static constexpr uint8_t BYTES_PER_SMALL_BLOCK = CONFIG.BYTES_PER_SMALL_BLOCK;

// check
    static_assert(ALIGN == 16 || ALIGN == 8 || ALIGN == 4, "ALIGN VALID VALUES: 4, 8, 16");
    static_assert(SIZE % ALIGN == 0, "SIZE MUST BE MULTIPLE OF ALIGN");

// forward
private:
    struct Chunk;

// STORAGE
private:
    Chunk baseChunk;

// API
public:
    void * alloc(size_t size, char const * label = nullptr) {
        void * ptr;
        for (Chunk * c = &baseChunk; c; c = c->nextChunk) {
            if ((ptr = c->alloc(size, label))) {
                return ptr;
            }
        }
        return nullptr;
    }

    bool free(void * ptr) {
        bool didFree;
        for (Chunk * c = &baseChunk; c; c = c->nextChunk) {
            if ((didFree = c->free(ptr))) {
                return true;
            }
        }
        return false;
    }

    #ifdef MEMMAN_ENABLE_PRINT
    void printStatus(char const * msg = "") const {
        for (Chunk const * c = &baseChunk; c; c = c->nextChunk) {
            c->printStatus(msg);
        }
    }
    #endif

// internals
private:

    static constexpr void * align(void * offset) {
        return (void *)(((size_t)offset + (ALIGN-1)) & ~(ALIGN-1));
    }
    static constexpr size_t align(size_t offset) {
        return (offset + (ALIGN-1)) & ~(ALIGN-1);
    }

    struct alignas(ALIGN) Chunk {


        struct alignas(ALIGN) Block {
            // 31-bits, about 2-GB byte limit for each block
            static constexpr size_t BlockMaxSize = 0x7FFFFFFF;
            static constexpr size_t LabelMax = ALIGN - (32 / BitsPerByte);

            bool claimed:1;
            uint32_t dataSize:31;
            char label[LabelMax];

            Block(size_t dataSize, char const * label, size_t labelLength) :
                claimed(false),
                dataSize(dataSize)
            {
                setLabel(label, labelLength);
            }

            void setLabel(char const * newLabel, size_t newLabelLength) {
                if constexpr (LabelMax) {
                    if (newLabelLength > LabelMax) {
                        newLabelLength = LabelMax;
                    }
                    strncpy(this->label, newLabel, newLabelLength);
                    this->label[newLabelLength] = '\0';
                }
            }

            void * data() const {
                return (void *)((byte_t *)this + sizeof(Block));
            }

            Block * next() const {
                return (Block *)(byte_t *)align((byte_t *)this + sizeof(Block) + dataSize);
            }

            bool claim(size_t newSize) {
                if (claimed) {
                    return false;
                }

                size_t alignedNewSize = (size_t)align((void *)newSize);

                // block not big enough
                if (alignedNewSize > dataSize) {
                    return false;
                }

                // split?
                // only split if there is enough room for a new block with
                // the ability to store ALIGN (arbitrary) bytes.
                if (alignedNewSize + sizeof(Block) + ALIGN <= dataSize) {
                    // split!
                    byte_t * splitBlockPtr = (byte_t *)this + sizeof(Block) + alignedNewSize;
                    size_t splitSize = dataSize - sizeof(Block) - alignedNewSize;
                    new (splitBlockPtr) Block{splitSize, "split", 5};
                }

                // claimed!
                claimed = true;
                dataSize = newSize;
                return true;
            }

            bool releaseAndMerge(Block * prev = nullptr) {
                if (claimed == false) {
                    return false;
                }

                claimed = false;

                merge(prev, this);
                merge(this, next());
                return true;
            }

            Block * prev(Block * first) const {
                if (this == first) {
                    return nullptr;
                }

                Block * b = first;
                Block * n;
                while (b) {
                    n = b->next();
                    if (this == n) {
                        return b;
                    }
                    b = n;
                }
                return nullptr;
            }

            #ifdef MEMMAN_ENABLE_PRINT
            void printStatus() const {
                printf("Block %p [%-*s] %s %10d, %p—%p\n",
                    this,
                    (int)LabelMax,
                    label,
                    claimed?"(claimed)":"(free)   ",
                    dataSize,
                    data(),
                    next()
                );
            }
            #endif

        private:
            bool merge(Block * a, Block * b) {
                if (!a || !b) {
                    return false;
                }
                if (a->claimed || b->claimed) {
                    return false;
                }
                a->dataSize = align(a->dataSize + sizeof(Block) + b->dataSize);
                return true;
            }
        }; // struct Block
        static_assert(sizeof(Block) == ALIGN, "BLOCK MUST SIZE OF ALIGN");


        struct alignas(ALIGN) SmallBlocks {
            friend struct Chunk;
        private:
            static constexpr size_t SmallBlockCount = [] {
                ssize_t count = SMALL_BLOCKS_SIZE / BYTES_PER_SMALL_BLOCK;
                while (count > 0) {
                    ssize_t freeListByteCount = (count + BitsPerByte - 1) / BitsPerByte;
                    if (count * BYTES_PER_SMALL_BLOCK + freeListByteCount <= SMALL_BLOCKS_SIZE) {
                        break;
                    }
                    --count;
                }
                if (count < 0) {
                    return (size_t)0;
                }
                return (size_t)count;
            }();

            /*
            `data` layout where n = SmallBlockCount:
            
            [ block 0 ][ block 1 ]...[ block n-1 ][ freelist ]

            (see freelist() for freelist sublayout)
            */
            byte_t data[SMALL_BLOCKS_SIZE];

            SmallBlocks() {
                memset(data, 0, SMALL_BLOCKS_SIZE);
            }

            void * operator[](size_t i) const {
                return (void *)((byte_t *)data + i * BYTES_PER_SMALL_BLOCK);
            }

            /*
            array of bytes, use freeListByteCount() for length
            byte 0 MSB = index 0
            */
            byte_t const * freeList() const {
                if constexpr (SMALL_BLOCKS_SIZE) {
                    // return the position direcly after the last block
                    return data + (SmallBlockCount * BYTES_PER_SMALL_BLOCK);
                }
                if constexpr (SMALL_BLOCKS_SIZE == 0) {
                    return nullptr;
                }
            }

            size_t freeListByteCount() const {
                if constexpr (SMALL_BLOCKS_SIZE) {
                    return (byte_t *)data + SMALL_BLOCKS_SIZE - freeList();
                }
                if constexpr (SMALL_BLOCKS_SIZE == 0) {
                    return 0;
                }
            }

            size_t firstFree() {
                byte_t * const start = (byte_t * const)freeList();
                byte_t * const end = data + SMALL_BLOCKS_SIZE;
                for (byte_t * flb = start; flb < end; ++flb) {
                    byte_t b = *flb;
                    // first byte that isn't "full"
                    if (b < 0xff) {
                        return (flb - start) * BitsPerByte + std::countl_one(b);
                    }
                }
                // not found
                return SmallBlockCount;
            }

            void * alloc(size_t size, char const * label = nullptr) {
                if (size > BYTES_PER_SMALL_BLOCK) {
                    return nullptr;
                }

                size_t ff = firstFree();
                if (ff >= SmallBlockCount) {
                    return nullptr;
                }

                // set free bit
                setFreeListBit(ff, true);

                return (*this)[ff];
            }

            void free(void * ptr) {
                // find index
                size_t ff = (size_t)((byte_t *)ptr - data) / BYTES_PER_SMALL_BLOCK;
                // unset free list bit
                setFreeListBit(ff, false);
            }

            void setFreeListBit(size_t i, bool value) {
                byte_t * freeListStart = (byte_t *)freeList();
                size_t byte = i / BitsPerByte;
                size_t bit  = i % BitsPerByte;
                static constexpr byte_t msbValue = 0b10000000;
                freeListStart[byte] = (value) ?
                    // set bit (bit is now 1, not free, claimed)
                        freeListStart[byte] | (msbValue >> bit) :
                    // clear bit (bit is now 0, free, not claimed)
                        freeListStart[byte] & ~(msbValue >> bit);
            }

            byte_t * next() const {
                return (byte_t *)data + SMALL_BLOCKS_SIZE;
            }

            #warning "not working?"
            #ifdef MEMMAN_ENABLE_PRINT
            void printStatus() const {
                static constexpr byte_t msbValue = 0b10000000;
                // byte_t check = *((SmallBlocks *)this)->freeList();
                size_t freeListByteCount = this->freeListByteCount();
                printf("SmallBlocks %p, block count: %zu\n", this, SmallBlockCount);
                printf("[");
                size_t i = 0;

                byte_t * fl = (byte_t *)freeList();
                for (size_t i = 0; i < SmallBlockCount; ++i) {
                    size_t byteI = i / BitsPerByte;
                    size_t bitI = i % BitsPerByte;
                    byte_t * byte = (byte_t *)fl + byteI;
                    printf("%s", (*byte & (msbValue >> bitI)) ?"X":"_");
                }
                printf("]\n");
            }
            #endif
        }; // struct SmallBlocks

        static constexpr size_t DATA_SIZE = SIZE - sizeof(Block *) - sizeof(Chunk *);
        static constexpr size_t INIT_BLOCK_DATA_SIZE = DATA_SIZE - SMALL_BLOCKS_SIZE - sizeof(Block);
        byte_t data[DATA_SIZE];
        Block * firstFree;
        Chunk * nextChunk;

        Chunk(Chunk * prevChunk = nullptr, Chunk * nextChunk = nullptr) :
            firstFree(new (firstBlock()) Block{INIT_BLOCK_DATA_SIZE, "init", 4}),
            nextChunk(nextChunk)
        {
            if constexpr (SMALL_BLOCKS_SIZE) {
                new (data) SmallBlocks{};
            }
        }

        Block * firstBlock() const {
            if constexpr (SMALL_BLOCKS_SIZE) {
                return (Block *)smallBlocks()->next();
            }
            if constexpr (SMALL_BLOCKS_SIZE == 0) {
                return (Block *)data;
            }
        }

        byte_t const * dataEnd() const {
            return data + DATA_SIZE;
        }

        SmallBlocks * smallBlocks() const {
            if constexpr (SMALL_BLOCKS_SIZE) {
                return (SmallBlocks *)data;
            }
        }

        void * alloc(size_t size, char const * label = nullptr) {
            if (size == 0) {
                return nullptr;
            }

            if constexpr (SMALL_BLOCKS_SIZE) {
                void * ptr;
                if ((ptr = smallBlocks()->alloc(size))) {
                    return ptr;
                }
            }

            Block * claimed = nullptr;
            for (Block * i = firstFree; i; i = i->next()) {
                if (i->claim(size)) {
                    claimed = i;

                    // was this firstFree?
                    if (firstFree == claimed) {
                        // if so find first free after this
                        byte_t const * chunkDataEnd = dataEnd();
                        for (Block * b = firstBlock(); (byte_t const *)b < chunkDataEnd; b = b->next()) {
                            if (b->claimed == false) {
                                firstFree = b;
                                break;
                            }
                        }
                    }

                    break;
                }
            }

            if (claimed) {
                return claimed->data();
            }

            if (!nextChunk) {
                return nullptr;
            }

            return nextChunk->alloc(size, label);
        }

        bool free(void * ptr) {
            // out of range
            if (ptr < data) {
                return false;
            }
            if constexpr (SMALL_BLOCKS_SIZE) {
                if (ptr < smallBlocks()->next()) {
                    smallBlocks()->free(ptr);
                    return true;
                }
            }
            if (ptr >= data + SIZE) {
                return false;
            }

            Block * block = blockForPtr(ptr);
            if (block) {
                Block * prev = block->prev(firstBlock());
                block->releaseAndMerge(prev);
            }
            return true;
        }

        Block * blockForPtr(void * ptr) {
            return (Block *)((byte_t *)ptr - sizeof(Block));
        }

        #ifdef MEMMAN_ENABLE_PRINT
        void printStatus(char const * msg) const {
            if (msg[0]) {
                printf("%s\n", msg);
            }
            printf("--------------------------------------------------------------------------------\n");
            byte_t const * de = dataEnd();
            printf("Chunk %zu (%p—%p]\n", SIZE, data, this + sizeof(Chunk));
            if constexpr (SMALL_BLOCKS_SIZE) {
                printf("-\n");
                smallBlocks()->printStatus();
            }
            printf("-\n");
            for (Block * b = firstBlock(); (byte_t const *)b < de; b = b->next()) {
                b->printStatus();
                printf("-\n");
            }
            printf("ffree %p %p (%s)\n", &firstFree, firstFree, (firstFree)?firstFree->label:"");
            printf("nextc %p %p\n", &nextChunk, nextChunk);
            printf("--------------------------------------------------------------------------------\n\n");
        }
        #endif
    }; // struct Chunk

};
