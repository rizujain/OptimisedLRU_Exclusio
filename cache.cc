// simulate a random or LRU cache

#include <stdio.h>
#include <assert.h>
#include "utils.h"
#include "replacement_state.h"
#include "cache.h"

using namespace std;

static unsigned int random_counter = 0;

void place (cache *c, unsigned long long int pc, unsigned int set, block *b, int offset) {
	// which pc filled this block

	b->filling_pc = pc;

	// which *byte* offset filled this block

	b->offset = offset;
}

// log base 2

int lg2 (int n) {
	int i, m = n, c = -1;
	for (i=0; m; i++) {
		m /= 2;
		c++;
	}
	assert (n == 1<<c);
	return c;
}

// make a cache.  hope blocksize and nsets are a power of 2.

void init_cache (cache *c, int nsets, int assoc, int blocksize, int replacement_policy, int set_shift) {
	int i, j;
	c->sets = new set[nsets];
	c->replacement_policy = replacement_policy;
	c->repl = new CACHE_REPLACEMENT_STATE (nsets, assoc, replacement_policy);
	c->set_shift = set_shift;
	c->nsets = nsets;
	c->assoc = assoc;
	c->blocksize = blocksize;
	c->offset_bits = lg2 (blocksize);
	c->index_bits = lg2 (nsets);
	c->tagshiftbits = c->offset_bits + c->index_bits;
	c->index_mask = nsets - 1;
	c->misses = 0;
	c->accesses = 0;
	memset (c->counts, 0, sizeof (c->counts));
	for (i=0; i<nsets; i++) {
		for (j=0; j<assoc; j++) {
			block *b = &c->sets[i].blocks[j];
			b->tag = 0;
			b->valid = 0;
			b->dirty = 0;
		}
		c->sets[i].valid = 0;
	}
}

// move a block to the MRU position

void move_to_mru (block *v, int i) {
	int j;
	block b = v[i];
	for (j=i; j>=1; j--) v[j] = v[j-1];
	v[0] = b;
}

// invalidate a block out of this cache! the block might not be there, but if it is, we'll blow it away

void invalidate (cache *c, unsigned long long int address) {
	int i, assoc = c->assoc;
	block *v;
	unsigned long long int block_addr = address >> c->offset_bits;
	unsigned long long int tag = block_addr >> c->index_bits;
	unsigned int set = (block_addr >> c->set_shift) & c->index_mask;
	v = &c->sets[set].blocks[0];
	for (i=0; i<assoc; i++) {
		if (v[i].tag == tag) {
			v[i].valid = 0;
			c->invalidations++;
			break;
		}
	}
	
}

// access a cache, return true for miss, false for hit

#define check_writeback(b) { if (writeback_address && v[(b)].valid && (v[(b)].dirty || (assoc!=16))) *writeback_address = ((v[(b)].tag << c->index_bits) + set) << c->offset_bits; }

bool cache_access (cache *c, unsigned long long int address, unsigned long long int pc, unsigned int size, int op, unsigned int core, unsigned long long int *writeback_address = NULL, bool do_place = true, int access_source = 0) {
	c->counts[op]++;
	int i, assoc = c->assoc;
	block *v;
	unsigned int offset = address & (c->blocksize - 1);
	unsigned long long int block_addr = address >> c->offset_bits;
	unsigned int set = (block_addr >> c->set_shift) & c->index_mask;

	// note this doesn't generate the right tag if we have a non-zero set shift
	// we *do* need the right tag value for things like the sampler to work
	// because the sampler recontstructs the physical address from the tag & index

	unsigned long long int tag = block_addr >> c->index_bits;

	// this will be true if the current set contains only valid blocks, false otherwise

	int set_valid = c->sets[set].valid;
	set_valid = false; // we can get back-invalidations so we can't use this optimization here
	c->accesses++;
	v = &c->sets[set].blocks[0];
	LINE_STATE ls;
	if (writeback_address) *writeback_address = 0;
	AccessTypes at;
	switch (op) {
		case DAN_PREFETCH: at = ACCESS_PREFETCH; break;
		case DAN_DREAD: at = ACCESS_LOAD; break;
		case DAN_WRITE: at = ACCESS_STORE; break;
		case DAN_WRITEBACK: at = ACCESS_WRITEBACK; break;
		case DAN_IREAD: at = ACCESS_IFETCH; break;
		default: at = ACCESS_LOAD;
		printf ("op is %d!\n", op); fflush (stdout);
		assert (0);
	}
	
	// tag match?

	for (i=0; i<assoc; i++) {
		if (v[i].tag == tag && v[i].valid) {
			if (at == ACCESS_STORE || at == ACCESS_WRITEBACK) v[i].dirty = true;
			if (c->replacement_policy == REPLACEMENT_POLICY_LRU) {
				// move this block to the mru position
				if (i != 0) move_to_mru (v, i);
				assert (i >= 0 && i < assoc);
				// update CRC's LRU policy (for instrumentation)
				ls.tag = tag;
				if (at != ACCESS_WRITEBACK)
					c->repl->UpdateReplacementState (set, i, &ls, core, pc, at, true, access_source);
			} else if (c->replacement_policy >= REPLACEMENT_POLICY_CRC) {
				ls.tag = tag;
				assert (i >= 0 && i < assoc);
				if (at != ACCESS_WRITEBACK)
					c->repl->UpdateReplacementState (set, i, &ls, core, pc, at, true, access_source);
			}
			return false;
		}
	}

	// a miss.

	c->misses++;

	// should we place this block in the cache? if not, just return

	if (!do_place) return true;

	// find a block to replace

	if (!set_valid) {
		for (i=0; i<assoc; i++) {
			if (v[i].valid == 0) break;
		}
		if (i == assoc) {
			c->sets[set].valid = 1; // mark this set as having only valid blocks so we don't search it again
			set_valid = 1;
		}
		// at this point, i indicates an invalid block, or assoc if there is no invalid block
	}
	if (c->replacement_policy == REPLACEMENT_POLICY_RANDOM) {

		// if no invalid block, choose a random one

		if (set_valid) i = (random_counter++) % assoc; // replace
		check_writeback (i);
		if (at == ACCESS_STORE || at == ACCESS_WRITEBACK) 
			v[i].dirty = true;
		else
			v[i].dirty = false;
		v[i].tag = tag;
		v[i].valid = 1;
		place (c, pc, set, &v[i], offset);
	} else if (c->replacement_policy == REPLACEMENT_POLICY_LRU) {

		// if no invalid block, use the lru one (the one in the last position)

		if (set_valid) i = assoc - 1; // replace LRU block
		check_writeback (i);
		if (i != 0) move_to_mru (v, i);
		if (at == ACCESS_STORE || at == ACCESS_WRITEBACK) 
			v[0].dirty = true;
		else
			v[0].dirty = false;
		v[0].tag = tag;
		v[0].valid = 1;
		place (c, pc, set, &v[0], offset);

		// update CRC's LRU policy (for instrumentation)
		ls.tag = tag;
		if (at != ACCESS_WRITEBACK) {
			// find LRU way
			int lru = -1;
			for (int z=0; z<(int)assoc; z++) if (c->repl->repl[set][z].LRUstackposition == (unsigned) assoc-1) { lru = z; break; }
			assert (lru >= 0);
			c->repl->UpdateReplacementState (set, lru, &ls, core, pc, at, false, access_source);
		}
	} else {
		// assume we are using CRC replacement policy, see what it wants to replace
		if (set_valid) {
			i = c->repl->GetVictimInSet (core, set, NULL, assoc, pc, address, at, access_source); // replace
		}
		ls.tag = tag;

		// -1 means bypass

		if (i != -1) {
			check_writeback (i);
			if (at == ACCESS_STORE || at == ACCESS_WRITEBACK) 
				v[i].dirty = true;
			else
				v[i].dirty = false;
			v[i].tag = tag;
			v[i].valid = 1;
			assert (i >= 0 && i < assoc);
			c->repl->UpdateReplacementState (set, i, &ls, core, pc, at, false, access_source);
			place (c, pc, set, &v[i], offset);
		}
	}
	// only count as a miss if the block is not a writeback block or prefetch
	//return (at != ACCESS_WRITEBACK) && (at != ACCESS_PREFETCH);
	// no, don't do that. we should always return true here; the other
	// behavior was to support something that is now deprecated
	return true;
}

// access the memory, returning an integer that has:
// bit 0 set if there is a miss in L1
// bit 1 set if there is a miss in L2
// bit 2 set if there is a miss in L3

// private L1 and L2, shared L3

unsigned int memory_access (cache *L1, cache *L2, cache *L3, unsigned long long int address, unsigned long long int pc, unsigned int size, int op, unsigned int core) {
	// access the memory hierarchy, returning latency of access
	unsigned int miss = 0;

	unsigned long long int wbl1;
	unsigned int missL1 = cache_access (&L1[core], address, pc, size, op, core, &wbl1, true, ACCESS_1);
        if (missL1) {
                miss |= MISS_L1_DEMAND;
		unsigned long long int wbl2;

		// see if the block is in the L2, but don't place it there if not

		unsigned int missL2 = cache_access (&L2[core], address, pc, size, op, core, &wbl2, false, ACCESS_2);
		if (missL2) {
			miss |= MISS_L2_DEMAND;
			unsigned long long int wbl3;
			// see if the block is in the shared LLC, but don't place it there if not
			bool missL3 = cache_access (L3, address, pc, size, op, core, &wbl3, false, ACCESS_3);
			if (missL3) miss |= MISS_L3_DEMAND;
			// if it is there, we need to invalidate out of the L2 and L3 for the L1 demand access
			invalidate (L3, address);
			invalidate (L2, address);
		} else {
			// no miss from L2; invalidate this out of the L2 if it is there
			invalidate (L2, address);
		}
		if (wbl1) {
			miss |= MISS_L1_WRITEBACK;
			// generate a writeback to L2
			unsigned long long int wbl2;
			// place this L1 victim in the L2
			(void) cache_access (&L2[core], wbl1, pc, size, DAN_WRITEBACK, core, &wbl2, true, ACCESS_4);
			if (wbl2) {
				// this writeback generated its own writeback
				miss |= MISS_L2_WRITEBACK;
				unsigned long long int wbl3;
				// place this L2 victim in the LLC
				unsigned int missL3 = cache_access (L3, wbl2, pc, size, DAN_WRITEBACK, core, &wbl3, true, ACCESS_5);
				if (wbl3) miss |= MISS_L3_WRITEBACK;
				// what if we missed didn't write back to DRAM?
				if (missL3) miss |= MISS_L3_DEMAND;
			}
		}
		if (wbl2) {
			// generate a writeback to L3
			if (miss & MISS_L2_WRITEBACK) miss |= MISS_L2_2ND_WRITEBACK; else miss |= MISS_L2_WRITEBACK;
			unsigned long long int wbl3;
			unsigned int missL3 = cache_access (L3, wbl2, pc, size, DAN_WRITEBACK, core, &wbl3, true, ACCESS_6);
			if (missL3) { if (miss & MISS_L3_WRITEBACK) miss |= MISS_L3_2ND_WRITEBACK; } else miss |= MISS_L3_WRITEBACK;
		}
	}
	return miss;
}
