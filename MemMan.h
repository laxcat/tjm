#pragma once
#include <stdlib.h>
#include <string.h>
#include <new>
#include <bit>

#warning "remove me"
#include <stdio.h>

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
    - Configure using

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
    size_t ALIGN = 16;
    size_t SMALL_BLOCKS_SIZE = 4_KB;
    size_t BYTES_PER_SMALL_BLOCK = ALIGN;
};

template <MemManConfig C>
class alignas(C.ALIGN) MemMan {

// check
    static_assert(C.ALIGN == 16 || C.ALIGN == 8 || C.ALIGN == 4, "ALIGN VALID VALUES: 4, 8, 16");
    static_assert(C.SIZE % C.ALIGN == 0, "SIZE MUST BE MULTIPLE OF ALIGN");

// forward
private:
    struct Chunk;

// STORAGE
private:
    Chunk baseChunk;

// API
public:
    void * alloc(size_t size, char const * label = nullptr) {
        for (Chunk * c = &baseChunk; c; c = c->nextChunk) {
            return c->alloc(size, label);
        }
        return nullptr;
    }

    void free(void * ptr) {
        for (Chunk * c = &baseChunk; c; c = c->nextChunk) {
            c->free(ptr);
        }
    }

// internals
private:

    static constexpr void * align(void * offset) {
        return (void *)(((size_t)offset + (C.ALIGN-1)) & ~(C.ALIGN-1));
    }
    static constexpr size_t align(size_t offset) {
        return (offset + (C.ALIGN-1)) & ~(C.ALIGN-1);
    }

    struct alignas(C.ALIGN) Chunk {


        struct alignas(C.ALIGN) Block {
        private:
            // 31-bits, about 2-GB byte limit for each block
            static constexpr size_t BlockMaxSize = 0x7FFFFFFF;
            static constexpr size_t LabelMax = C.ALIGN - (32 / BitsPerByte);

            bool claimed:1;
            uint32_t size:31;
            char label[LabelMax];

        public:
            Block(size_t blockSize, char const * label, size_t labelLength) :
                claimed(false),
                size(blockSize)
            {
                setLabel(label, labelLength);
            }

            void setLabel(char const * label, size_t labelLength) {
                if constexpr (LabelMax) {
                    if (labelLength > LabelMax) {
                        labelLength = LabelMax;
                    }
                    strncpy(this->label, label, labelLength);
                    this->label[labelLength-1] = '\0';
                }
            }

            void * data() const {
                return (void *)((byte_t *)this + sizeof(Block));
            }

            Block * next() const {
                return (Block *)(byte_t *)align((byte_t *)this + sizeof(Block) + size);
            }

            bool claim(size_t newSize) {
                if (claimed) {
                    return false;
                }

                size_t alignedNewSize = (size_t)align((void *)newSize);

                // block not big enough
                if (alignedNewSize > size) {
                    return false;
                }

                // split?
                // only split if there is enough room for a new block with
                // the ability to store ALIGN (arbitrary) bytes.
                if (alignedNewSize + sizeof(Block) + C.ALIGN <= size) {
                    // split!
                    new (next()) Block{size - sizeof(Block) - alignedNewSize, "split", 5};
                }

                // claimed!
                claimed = true;
                size = newSize;
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

        private:
            bool merge(Block * a, Block * b) {
                if (!a || !b) {
                    return false;
                }
                if (a->claimed || b->claimed) {
                    return false;
                }
                a->size += sizeof(Block) + b->size;
                return true;
            }
        }; // struct Block
        static_assert(sizeof(Block) == C.ALIGN, "BLOCK MUST SIZE OF ALIGN");


        struct alignas(C.ALIGN) SmallBlocks {
            friend struct Chunk;
        private:
            static constexpr size_t SmallBlockCount = [] {
                ssize_t count = C.SMALL_BLOCKS_SIZE / C.BYTES_PER_SMALL_BLOCK;
                while (count > 0) {
                    ssize_t freeListByteCount = (count + BitsPerByte - 1) / BitsPerByte;
                    if (count * C.BYTES_PER_SMALL_BLOCK + freeListByteCount <= C.SMALL_BLOCKS_SIZE) {
                        break;
                    }
                    --count;
                }
                if (count < 0) {
                    return (size_t)0;
                }
                return (size_t)count;
            }();

            byte_t data[C.SMALL_BLOCKS_SIZE];

            SmallBlocks() {
                printf("Small Blocks count    %zu\n", SmallBlockCount);
                printf("Small Blocks data     %p\n",  data);
                printf("Small Blocks freelist %p\n",  freeList());
                memset(data, 0, C.SMALL_BLOCKS_SIZE);
            }

            void * operator[](size_t i) const {
                return (void *)((byte_t *)data + i * C.BYTES_PER_SMALL_BLOCK);
            }

            byte_t * freeList() {
                return data + (SmallBlockCount * C.BYTES_PER_SMALL_BLOCK);
            }

            size_t firstFree() {
                byte_t * const start = freeList();
                byte_t * const end = data + C.SMALL_BLOCKS_SIZE;
                for (byte_t * flb = start; flb < end; ++flb) {
                    byte_t b = *flb;
                    // first byte that isn't "full"
                    if (b < 0xff) {
                        return (flb - start) * BitsPerByte + std::countr_one(b);
                    }
                }
                // not found
                return SmallBlockCount;
            }

            void * alloc(size_t size, char const * label = nullptr) {
                if (size > C.BYTES_PER_SMALL_BLOCK) {
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
                size_t ff = (size_t)((byte_t *)ptr - data) / C.BYTES_PER_SMALL_BLOCK;
                // unset free list bit
                setFreeListBit(ff, false);
            }

            void setFreeListBit(size_t i, bool value) {
                byte_t * freeListStart = freeList();
                size_t byte = i / BitsPerByte;
                size_t bit  = i % BitsPerByte;
                freeListStart[byte] = (value) ?
                    // set bit (bit is now 1, not free, claimed)
                        freeListStart[byte] | ((byte_t)1 << bit) :
                    // clear bit (bit is now 0, free, not claimed)
                        freeListStart[byte] & ~((byte_t)1 << bit);
            }

            byte_t * next() const {
                return (byte_t *)data + C.SMALL_BLOCKS_SIZE;
            }

        }; // struct SmallBlocks

        Block * firstFree;
        Chunk * nextChunk;
        static constexpr size_t DATA_SIZE = C.SIZE - sizeof(Block *) - sizeof(Chunk *);
        byte_t data[DATA_SIZE];

        Chunk(Chunk * prevChunk = nullptr, Chunk * nextChunk = nullptr) :
            firstFree(new (firstBlock()) Block{DATA_SIZE - sizeof(Block), "init", 4}),
            nextChunk(nextChunk)
        {
            printf("Chunk data            %p\n", data);
            printf("Chunk firstBlock      %p\n", firstBlock());
            if constexpr (C.SMALL_BLOCKS_SIZE) {
                new (data) SmallBlocks{};
            }
        }

        Block * firstBlock() const {
            if constexpr (C.SMALL_BLOCKS_SIZE) {
                return (Block *)smallBlocks()->next();
            }
            if constexpr (C.SMALL_BLOCKS_SIZE == 0) {
                return (Block *)data;
            }
        }

        SmallBlocks * smallBlocks() const {
            if constexpr (C.SMALL_BLOCKS_SIZE) {
                return (SmallBlocks *)data;
            }
        }

        void * alloc(size_t size, char const * label = nullptr) {
            if (size == 0) {
                return nullptr;
            }

            if constexpr (C.SMALL_BLOCKS_SIZE) {
                void * ptr;
                if ((ptr = smallBlocks()->alloc(size))) {
                    return ptr;
                }
            }

            Block * claimed = nullptr;
            for (Block * i = firstFree; i; i = i->next()) {
                if (i->claim(size)) {
                    claimed = i;
                    if (claimed < firstFree) {
                        claimed = firstFree;
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

        void free(void * ptr) {
            // out of range
            if (ptr < data) {
                return;
            }
            if constexpr (C.SMALL_BLOCKS_SIZE) {
                if (ptr < smallBlocks()->next()) {
                    smallBlocks()->free(ptr);
                    return;
                }
            }
            if (ptr >= data + C.SIZE) {
                return;
            }

            Block * block = blockForPtr(ptr);
            if (block) {
                Block * prev = block->prev(firstBlock());
                block->releaseAndMerge(prev);
            }
        }

        Block * blockForPtr(void * ptr) {
            return (Block *)((byte_t *)ptr - sizeof(Block));
        }
    }; // struct Chunk

};
