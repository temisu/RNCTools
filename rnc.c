/*
 * rnc.c   Compress a file as RNC
 *
 * Requires dernc.c to compute the leeway header field.
 * 
 * originally issued from Dungeon Keeper utilities
 * (http://www.yoda.arachsys.com/dk/utils.html)
 *
 * adapted/ported to 32 bit by JOTD
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>  // JOTD replaced "int" (probably 16 bit) by standard 16 bit int

#define INTERNAL
#include "dernc.h"

typedef struct {
    struct {
	unsigned long code;
	int16_t codelen;
    } table[32];
} huf_table;

static void *rnc_pack (void *data, long datalen, long *packlen);
static void do_block (void);
static void write_block (void);
static void build_huf (huf_table *h, int16_t *freqs);
static void write_huf (huf_table *h);
static void write_hval (huf_table *h, unsigned value);
static void emit_raw (int16_t n);
static void emit_pair (int16_t pos, int16_t len);
static void write_bits (unsigned value, int16_t nbits);
static void write_literal (unsigned value);
static void check_size (void);
static int16_t length (unsigned value);
static unsigned long mirror (unsigned long x, int16_t n);
static void bwrite (unsigned char *p, unsigned long val);
static int16_t main_pack (char *pname, char *iname, char *oname);

int main(int argc, char **argv) {

    int16_t mode=0;  // JOTD: wasn't initialized
    int16_t i;
    if (argc==1)
    {
	fprintf(stderr, "usage: %s <files> or %s -o <infile> <outfile>\n", 
		*argv, *argv);
	return 0;
    }
    for (i=1; i < argc; i++)
	if (!strcmp (argv[1], "-o"))
	    mode=i;
    if (mode && argc != 4)
    {
	fprintf(stderr, "usage: %s <files> or %s -o <infile> <outfile>\n", 
		*argv, *argv);
	return 1;
    }
    switch (mode)
    {
      case 0 :
	for (i=1; i < argc; i++)
	    if (main_pack (*argv, argv[i], argv[i]))
		return 1;
	return 0;
      case 1 :
	return main_pack (*argv, argv[2], argv[3]);
      case 2 :
	return main_pack (*argv, argv[1], argv[3]);
      case 3 :
	return main_pack (*argv, argv[1], argv[2]);
      default :
	fprintf (stderr, "Internal fault.\n");
    }
    return 1;
}
    
int16_t main_pack (char *pname, char *iname, char *oname)
{    
    FILE *ifp, *ofp;
    long ulen, ulen2, plen;
    void *packed, *unpacked, *unpacked2;
    long leeway;

    ifp = fopen(iname, "rb");
    if (!ifp) {
	perror(iname);
	return 1;
    }

    fseek (ifp, 0L, SEEK_END);
    ulen = ftell (ifp);
    rewind (ifp);

    unpacked = malloc(ulen);
    if (!unpacked) {
	perror(pname);
	return 1;
    }
    fread (unpacked, 1, ulen, ifp);
    fclose (ifp);

	// JOTD added that test to avoid "huffman decode error" on an already RNC packed file
	if ((ulen > 4) && (strncmp("RNC\01",unpacked,4)==0))
	{
	fprintf(stderr, "File '%s' seems to be already RNC1 packed\n",iname);
	return 1;
	}
	
    packed = rnc_pack (unpacked, ulen, &plen);
    if (!packed) {
	fprintf(stderr, "Error in compression\n");
	return 1;
    }

    unpacked2 = malloc(ulen);
    if (!unpacked2) {
	perror(pname);
	return 1;
    }

    ulen2 = rnc_unpack (packed, unpacked2, &leeway);
    if (ulen2 < 0) {
	fprintf(stderr, "Test unpack: %s\n", rnc_error (ulen2));
	return 1;
    }

    if (ulen2 != ulen) {
	fprintf(stderr, "Test unpack: lengths do not match\n");
	return 1;
    }
    if (memcmp (unpacked, unpacked2, ulen)) {
	fprintf(stderr, "Test unpack: files do not match\n");
	return 1;
    }

    if (leeway > 255) {
	fprintf(stderr, "Unable to handle leeway > 255\n");
	return 1;
    }
    ((unsigned char *)packed)[16] = leeway;

    ofp = fopen(oname, "wb");
    if (!ofp) {
	perror(oname);
	return 1;
    }
    fwrite (packed, 1, plen, ofp);
    fclose (ofp);

    free (unpacked2);
    free (packed);
    free (unpacked);

    return 0;
}

#define BLOCKMAX 8192
#define WINMAX 32767
#define MAXTUPLES 4096
#define HASHMAX 509		       /* it's prime */
#define PACKED_DELTA 4096

typedef struct {
    int16_t rawlen;
    int16_t pos;
    int16_t len;
} tuple;

unsigned char blk[WINMAX];
int16_t linkp[WINMAX];
int16_t hashp[HASHMAX];
int16_t blkstart, bpos;
int16_t blklen;

tuple tuples[MAXTUPLES];
int16_t ntuple;

unsigned char *packed;
long packedlen;
long packedpos, bitpos, bitcount, bitbuf;

// JOTD: the original hash function is sooo weak and buggy
// because it cannot return a value over 255 (all computations are done
// using unsigned chars)
/*int16_t hash (unsigned char *a) {
    return ((a[0] * 7 + a[1]) * 7 + a[2]) % HASHMAX;
}*/
int16_t hash (unsigned char *a) {
    return (((unsigned int)a[0] * 7 + (unsigned int)a[1]) * 7 + (unsigned int)a[2]) % HASHMAX;
}
static void *rnc_pack (void *original, long datalen, long *packlen) {
    int16_t i;
    char *data = original;
    long origlen = datalen;
    int chunks = 0;
	
    packed = malloc(PACKED_DELTA);
    if (!packed) {
	perror("malloc");
	exit(1);
    }
    packedlen = PACKED_DELTA;    
    packedpos = 20;
    bwrite (packed+4, datalen);

    bitpos = 18;
    bitcount = 0;
    bitbuf = 0;
    write_bits (0, 2);


    while (datalen > 0) {
	blklen = datalen > BLOCKMAX ? BLOCKMAX : datalen;
	blkstart = WINMAX - BLOCKMAX;
	if (blkstart > origlen-datalen)
	    blkstart = origlen-datalen;
	memcpy (blk, data-blkstart, blkstart+blklen);
	for (i=0; i<HASHMAX; i++)
	    hashp[i] = -1;
	ntuple = 0;
	tuples[ntuple].rawlen = 0;
	blklen += blkstart;
	do_block();
	data += bpos - blkstart;
	datalen -= bpos - blkstart;
	write_block();
	chunks++;  // added by temisu or number of chunks is incorrect in header
    }

    if (bitcount > 0) {
	write_bits (0, 17-bitcount);   /* force flush */
	packedpos -= 2;		       /* write_bits will have moved it on */
    }

    *packlen = packedpos;

    bwrite (packed, RNC_SIGNATURE);
    bwrite (packed+12, rnc_crc(packed+18, packedpos-18));
    bwrite (packed+10, rnc_crc(original, origlen));
    bwrite (packed+8, packedpos-18);
    packed[16] = 0;
	packed[17] = chunks;  // added by temisu or number of chunks is incorrect in header

    return packed;
}

static void do_block (void) {
    int16_t lazylen = 0, lazypos = 0, lazyraw = 0;
    int16_t hashv, ph, h;

    bpos = 0;
    while (bpos < blkstart) {
	hashv = hash(blk+bpos);
	h = hashp[hashv];
	ph = -1;
	while (h != -1) {
	    ph = h;
	    h = linkp[h];
	}
	if (ph != -1)
	    linkp[ph] = bpos;
	else
	    hashp[hashv] = bpos;
	linkp[bpos] = -1;
	bpos++;
    }

    while (bpos < blklen && ntuple < MAXTUPLES-1) {
	if (blklen - bpos < 3)
	    emit_raw (blklen - bpos);
	else {
	    int16_t len, maxlen, maxlenpos;
	    int16_t savebpos;

	    hashv = hash(blk+bpos);
	    h = hashp[hashv];

	    maxlen = 0;
	    maxlenpos = ph = -1;

	    while (h != -1) {
		unsigned char *p = blk+bpos;
		unsigned char *v = blk+h;
		len = 0;
		while (*p == *v && p < blk+blklen)
		    p++, v++, len++;
		if (maxlen < len) {
		    maxlen = len;
		    maxlenpos = h;
		}
		ph = h;
		h = linkp[h];
	    }

	    if (ph != -1)
		linkp[ph] = bpos;
	    else
		hashp[hashv] = bpos;

	    linkp[bpos] = -1;
	    savebpos = bpos;

	    bpos -= lazyraw;
	    if (lazyraw) {
		if (maxlen >= lazylen+2) {
		    emit_raw (lazyraw);
		    lazyraw = 1;
		    lazypos = maxlenpos;
		    lazylen = maxlen;
		} else {
		    emit_pair (lazypos, lazylen);
		    lazyraw = lazypos = lazylen = 0;
		}
	    } else if (maxlen >= 3) {
		lazyraw = 1;
		lazypos = maxlenpos;
		lazylen = maxlen;
	    } else {
		emit_raw (1);
	    }
	    bpos += lazyraw;

	    while (++savebpos < bpos) {
		hashv = hash(blk+savebpos);
		h = hashp[hashv];
		ph = -1;
		while (h != -1) {
		    ph = h;
		    h = linkp[h];
		}
		if (ph != -1)
		    linkp[ph] = savebpos;
		else
		    hashp[hashv] = savebpos;
		linkp[savebpos] = -1;
	    }	    
	}
    }
    if (lazyraw) {
	bpos -= lazyraw;
	emit_raw (lazyraw);
    }
}

static void write_block (void) {
    int16_t lengths[32];
    huf_table raw, dist, len;
    int16_t i, j, k;

    for (i=0; i<32; i++)
	lengths[i] = 0;
    for (i=0; i<=ntuple; i++)
	lengths[length(tuples[i].rawlen)]++;
    build_huf (&raw, lengths);
    write_huf (&raw);

    for (i=0; i<32; i++)
	lengths[i] = 0;
    for (i=0; i<ntuple; i++)
	lengths[length(tuples[i].pos-1)]++;
    build_huf (&dist, lengths);
    write_huf (&dist);

    for (i=0; i<32; i++)
	lengths[i] = 0;
    for (i=0; i<ntuple; i++)
	lengths[length(tuples[i].len-2)]++;
    build_huf (&len, lengths);
    write_huf (&len);

    write_bits (ntuple+1, 16);

    k = blkstart;
    for (i=0; i<=ntuple; i++) {
	write_hval (&raw, tuples[i].rawlen);
	for (j=0; j<tuples[i].rawlen; j++)
	    write_literal (blk[k++]);
	if (i == ntuple)
	    break;
	write_hval (&dist, tuples[i].pos-1);
	write_hval (&len, tuples[i].len-2);
	k += tuples[i].len;
    }
}

static void build_huf (huf_table *h, int16_t *freqs) {
    struct hnode {
	int16_t freq, parent, lchild, rchild;
    } pool[64];
    int16_t i, j, k, m, toobig;
    int16_t maxcodelen;
    unsigned long codeb;

    j = 0;			       /* j counts nodes in the pool */
    toobig = 1;
    for (i=0; i<32; i++)
	if (freqs[i]) {
	    pool[j].freq = freqs[i];
	    pool[j].parent = -1;
	    pool[j].lchild = -1;
	    pool[j].rchild = i;
	    j++;
	    toobig += freqs[i];
	}

    k = j;			       /* k counts _free_ nodes in the pool */
    while (k > 1) {
	int16_t min = toobig;
	int16_t nextmin = toobig+1;
	int16_t minpos, nextminpos;
	minpos = 0;
	for (i=0; i<j; i++)	       /* loop through free nodes */
	    if (pool[i].parent == -1) {
		if (min > pool[i].freq) {
		    nextmin = min;
		    nextminpos = minpos;
		    min = pool[i].freq;
		    minpos = i;
		} else if (nextmin > pool[i].freq) {
		    nextmin = pool[i].freq;
		    nextminpos = i;
		}
	    }
	pool[j].freq = min + nextmin;
	pool[j].parent = -1;
	pool[j].lchild = minpos;
	pool[j].rchild = nextminpos;
	pool[minpos].parent = j;
	pool[nextminpos].parent = j;
	j++;
	k--;
    }

    for (i=0; i<32; i++)
		h->table[i].codelen = 0;

	// temisu: fix a case where symbol is only once in the huffman table. This is an ugly hack...
	if (j == 1) {
		for (i = 0; i < 32; i++) if (freqs[i]) {
			h->table[i].codelen = 1;
			h->table[i].code = 0;
			break;
		}
		return;
	}

    maxcodelen = 0;
    for (i=0; i<j; i++)		       /* loop through original nodes */
	if (pool[i].lchild == -1) {
	    m = 0;
	    k = i;
	    while (pool[k].parent != -1)
		m++, k = pool[k].parent;
	    h->table[pool[i].rchild].codelen = m;
	    if (maxcodelen < m)
		maxcodelen = m;
	}

    codeb = 0;
    for (i=1; i<=maxcodelen; i++) {
	for (j=0; j<32; j++)
	    if (h->table[j].codelen == i) {
		h->table[j].code = mirror (codeb, i);
		codeb++;
	    }
	codeb <<= 1;
    }
}

static void write_huf (huf_table *h) {
    int16_t i, j;

    j = 0;
    for (i=0; i<32; i++)
	if (h->table[i].codelen > 0)
	    j = i+1;

    write_bits (j, 5);

    for (i=0; i<j; i++)
	write_bits (h->table[i].codelen, 4);
}

static void write_hval (huf_table *h, unsigned value) {
    int16_t len = length(value);

    write_bits (h->table[len].code, h->table[len].codelen);
    if (len >= 2)
	write_bits (value, len-1);
}

static void emit_raw (int16_t n) {
    tuples[ntuple].rawlen += n;
    bpos += n;
}

static void emit_pair (int16_t pos, int16_t len) {
    tuples[ntuple].pos = bpos - pos;
    tuples[ntuple].len = len;
    tuples[++ntuple].rawlen = 0;
    bpos += len;
}

static void write_bits (unsigned value, int16_t nbits) {
    value &= (1 << nbits)-1;
    bitbuf |= (value << bitcount);
    bitcount += nbits;

    if (bitcount > 16) {
	packed[bitpos] = bitbuf;
	bitbuf >>= 8;
	packed[bitpos+1] = bitbuf;
	bitbuf >>= 8;
	bitcount -= 16;
	bitpos = packedpos;
	packedpos += 2;
	check_size();
    }
}

static void write_literal (unsigned value) {
    packed[packedpos++] = value;
    check_size();
}

static void check_size (void) {
    if (packedpos > packedlen - 16) {
	packedlen += PACKED_DELTA;
	packed = realloc(packed, packedlen);
	if (!packed) {
	    perror("realloc");
	    exit(1);
	}
    }
}

static int16_t length (unsigned value) {
    int16_t ret = 0;
    while (value != 0)
	value >>= 1, ret++;
    return ret;
}

/*
 * Mirror the bottom n bits of x.
 */
static unsigned long mirror (unsigned long x, int16_t n) {
    unsigned long top = 1 << (n-1), bottom = 1;
    while (top > bottom) {
	unsigned long mask = top | bottom;
	unsigned long masked = x & mask;
	if (masked != 0 && masked != mask)
	    x ^= mask;
	top >>= 1;
	bottom <<= 1;
    }
    return x;
}

/*
 * Write big-endian long.
 */
static void bwrite (unsigned char *p, unsigned long val) {
    p[3] = val;
    val >>= 8;
    p[2] = val;
    val >>= 8;
    p[1] = val;
    val >>= 8;
    p[0] = val;
}
