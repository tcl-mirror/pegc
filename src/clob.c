#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "clob.h"
#include "vappendf.h"

#if CLOB_USE_ZLIB
#  include <zlib.h>
#endif

#define MARKER printf("MARKER: %s:%s:%d\n",__FILE__,__FUNCTION__,__LINE__)

#define CLOB_DEBUG 0

/** CLOB_INIT(B) is a convenience form of clob_init(B,0,0). */
#define CLOB_INIT(B) clob_init(B,0,0)


const ClobRC_t ClobRC = {0, /*  OK */
			 -1, /* Err */
			 -2, /* AllocError */
			 -3, /* UnexpectedNull */
			 -4, /* RangeError */
			 -5, /* IOError */
			 -6  /* ArgError */
};

struct Clob
{
	/*
	  The address of the memory this blob refers to.  See below
	  for more info.

	  If aData is pointed to a const string or a string owned by
	  another entity, then nAlloc must be set to 0. This indicates
	  to the framework that aData is not to be deleted when the
	  blob is freed. In this case, nUsed must be set to the full
	  length of the pointed-to data object. When shallow copies
	  like this exist, any change made to pointed-to copies will
	  invalidate the state of the doing-the-pointing Clob object.

	  The framework tries to ensure that aData[nUsed] is always
	  set to 0 (assuming that aData is not manipulated outside of
	  the clob API). Note that since aData may contain binary data
	  (and embedded nulls), such a trailing null cannot be used
	  for length determination purposes.
   	 */
	char * aData;
	/**
	  long nAlloc = the number of contiguous bytes allocated to
	  aData.  See below for more info.
	*/
	unsigned long nAlloc;
	/*
	  long nUsed = the number of contiguous bytes "used" in the
	  aData array.
	*/
	unsigned long nUsed;
	/*
	  The current position marker for some read operations.
	*/
	unsigned long nCursor;
};


/*
  Clob_empty is an empty Clob used for init purposes.
*/
static const Clob Clob_empty = {0, /* aData */
				0, /* nAlloc */
				0, /* nUsed */
				0 /* nCursor */
};

/**
   Default alloc size policy, simply returns n.
*/
static long clob_default_alloc_policy( long n )
{
	return n;
}

/**
   Alloc policy which returns n * 1.2.
*/
long clob_120_alloc_policy( long n )
{
	return (long) (n * 1.2);
}


typedef clob_alloc_policy_t RePol;
static RePol clob_current_alloc_policy = clob_default_alloc_policy;
RePol clob_set_alloc_policy( RePol f )
{
	RePol old = clob_current_alloc_policy;
	clob_current_alloc_policy = f ? f : clob_default_alloc_policy;
	return old;
}



#define CLOB_DUMP(X,B) if(CLOB_DEBUG) { printf(X ": blob [%s]: ", # B ); clob_dump(B,1); }
void clob_dump( Clob * cb, int doString )
{
	Clob * dest;
	CLOB_INIT(&dest);
	clob_appendf( dest,
		      "Clob@%p[nUsed=%d, nAlloc=%d, nCursor=%d][data@%p]",
		      cb, cb->nUsed, cb->nAlloc, cb->nCursor, cb->aData
		       //,(doString ? (cb->nUsed ? cb->aData : "NULL") : "...")
		       );
	if( doString )
	{
		if( cb->nAlloc && cb->aData[0] )
		{
			clob_appendf( dest, "=[%s]", clob_buffer(cb) );
		}
		else
		{
			clob_appendf( dest, "=[NULL]", cb );
		}
	}
        fappendf( stdout, "%s\n", clob_buffer( dest ) );
	clob_finalize( dest );
}


long clob_reset( Clob * cb )
{
	if( cb )
	{
#if CLOB_DEBUG
		printf( "Freeing clob @ [%p], bytes=%ld\n", cb, cb->nAlloc );
		// endless loop: clob_dump(cb,1);
#endif
		if( cb->nAlloc )
		{
			memset( cb->aData, 0, cb->nAlloc );
			free( cb->aData );
                        cb->aData = 0;
		}
		*cb = Clob_empty;
		return ClobRC.OK;
	}
	return ClobRC.UnexpectedNull;
}

long clob_finalize( Clob * cb )
{
    if( ! cb ) return ClobRC.UnexpectedNull;
    clob_reset(cb);
    free( cb );
    return ClobRC.OK;
}

void clob_force_in_bounds( Clob * cb )
{
	if( cb->nUsed >= cb->nAlloc ) cb->nUsed = cb->nAlloc - 1;
	if( cb->nUsed < 0 ) cb->nUsed = 0;
	//if( cb->nCursor > cb->nAlloc ) cb->nCursor = cb->nAlloc - 1;
	if( cb->nCursor > cb->nUsed ) cb->nCursor = cb->nUsed;
	if( cb->nCursor < 0 ) cb->nCursor = 0;
}

long clob_realloc( Clob * cb, unsigned long sz )
{
	static const int fudge = 1;
	/* ^^^ over-allocate by 1 to ensure we have space for
	   a trailing 0. */
	const int shrinkage = 512;
	if( 0 == sz )
	{
		clob_reset( cb );
	}
	else if( (sz > cb->nAlloc) ||
		 (sz < (cb->nAlloc - shrinkage))
		 )
	{
		char const * zOld = cb->aData;
		long oldUsed = cb->nUsed;
		long oldAlloc = cb->nAlloc;
		long allocsize = fudge + (*clob_current_alloc_policy)(sz);
		if( allocsize < (fudge + sz) ) allocsize = fudge + sz;
		char * pNew = oldAlloc
			? realloc( cb->aData, allocsize )
			: malloc( fudge + allocsize );
		if( ! pNew ) return ClobRC.AllocError;
		if( !oldAlloc )
		{ /** cb has/had no data */
			if( zOld )
			{ /* cb was pointing to shared data. Copy it. */
				memcpy( pNew, zOld, (oldUsed > allocsize) ? allocsize : oldUsed );
			}
			else
			{ /* cb had no buffer - create one. */
				memset( pNew, 0, allocsize );
				cb->nUsed = cb->nCursor = 0;
			}
		}
		else
		{ /** cb had data and we realloced. Zero out any new
		      memory. */
			if( oldAlloc && (oldAlloc < allocsize) )
			{
				memset( pNew + oldAlloc, 0, allocsize - oldAlloc);
			}
		}
		pNew[allocsize-fudge] = 0;
		cb->aData = pNew;
		cb->nAlloc = allocsize;
		clob_force_in_bounds( cb );
	}
	return cb->nAlloc;
}

long clob_size( Clob const * cb ) { return cb ? cb->nUsed : ClobRC.UnexpectedNull; }
long clob_capacity( Clob const * cb ) { return cb ? cb->nAlloc : ClobRC.UnexpectedNull; }
char * clob_buffer( Clob * cb ) { return cb ? cb->aData : 0; }
char const * clob_bufferc( Clob const * cb ) { return cb ? cb->aData : 0; }

long clob_resize( Clob * cb, unsigned long sz )
{
    unsigned long ret = clob_realloc( cb, sz );
    if( ret >= sz )
    {
	cb->nUsed = sz;
	cb->aData[sz] = 0;
    }
    return ret;
}

#if 0
/*
  NOT part of the public API!

  sz is forced into bounds (>=0 and <=cb->nAlloc) then cb->nUsed is
  set to sz. cb->aData[sz] is set to 0 to enforce the null-termination
  policy.
*/
static long clob_set_used_size( Clob * cb, long sz )
{
    if( sz < 0 ) sz = 0;
    else if( sz > cb->nAlloc ) sz = cb->nAlloc-1;
    cb->nUsed = sz;
    cb->aData[sz] = 0;
    return sz;
}
#endif // 0|1

Clob * clob_new()
{
    Clob * c = 0;
    clob_init(&c,0,0);
    return c;
}

long clob_init( Clob ** cb, char const * data, long n )
{
	if( ! cb )
	{
		return ClobRC.UnexpectedNull;
	}
	*cb = (Clob *) malloc( sizeof(Clob) );
	if( ! *cb )
	{
		return ClobRC.AllocError;
	}
	*(*cb) = Clob_empty;
	if( !data )
	{
		if( n < 1 ) return ClobRC.OK;
	}
	else
	{
		if( !n ) return ClobRC.OK;
	}
	if( data && (n < 0) ) n = strlen( data );
	long rc = clob_realloc( *cb, n );
	if( rc < ClobRC.OK )
	{
		free( *cb );
		*cb = 0;
		return rc;
	}
	if( data )
	{
		memcpy( (*cb)->aData, data, n );
	}
	return ClobRC.OK;
}


long clob_seek( Clob * cb, long offset, int whence )
{
	long pos = cb->nCursor;
	switch( whence )
	{
	  case CLOB_SEEK_SET:
		  pos = offset;
		  break;
	  case CLOB_SEEK_CUR:
		  pos += offset;
		  break;
	  case CLOB_SEEK_END:
		  pos = cb->nUsed + offset;
		  break;
	  default:
		  return ClobRC.RangeError;
	};
	if( pos < 0 ) pos = 0;
	else if( pos > cb->nUsed ) pos = cb->nUsed;
	return (cb->nCursor = pos);
}

void clob_rewind( Clob * cb )
{
	cb->nCursor = 0;
}

long clob_tell( Clob * cb )
{
	return cb->nCursor;
}

long clob_pos_in_bounds( Clob * cb, long pos )
{
	return (cb && (pos >= 0) && (pos < cb->nUsed))
		? ClobRC.OK
		: ClobRC.RangeError;
}

long clob_char_filln( Clob * cb, char ch, long startPos, long n )
{
	if( ! cb->nAlloc ) return ClobRC.Err;
	if( n <= 0 ) return ClobRC.RangeError;
	long rc = clob_pos_in_bounds(cb,startPos);
	if( ClobRC.OK !=  rc ) return rc;
	//if( ClobRC.OK ==  rc ) rc = clob_is_in_bounds(cb,startPos + n);
	if( (startPos + n) > cb->nAlloc ) n = cb->nAlloc - startPos;
	memset( cb->aData + startPos, ch, n );
	return n;
}

long clob_zero_fill( Clob * cb )
{
	return clob_char_filln( cb, '\0', 0, cb->nAlloc );
}

/**
   This function ensures that the one-past-the-last item in the blob
   to 0.  The "used" size of cb does not change.

   Returns one of the values defined in ClobRC:

   - ClobRC.AllocError if a memory (re)allocation fails.

   - ClobRC.OK on success.
*/
long clob_null_terminate( Clob * cb )
{
	if( ! cb->nAlloc ) return ClobRC.Err; // do not modify a data obj we don't own.
	if( (cb->nUsed + 1) >= cb->nAlloc )
	{
		long rc = clob_realloc( cb, cb->nUsed + 1 );
		if( rc < ClobRC.OK ) return rc;
	}
	cb->aData[cb->nUsed] = 0;
	return ClobRC.OK;
}

/**
   Writes n bytes of data from cb, starting at startPos. It expands cb
   if needed. It does NOT move the cursor.

   If n is -1 then strlen(data) is used for n.

   On success returns the number of bytes added. On failure it returns
   a negative integer error code defined in ClobRC. If either data or
   dsize are 0 then 0 is returned. If cb is null then
   ClobRC.UnexpectedNull is returned.
*/
static long clob_writeat( Clob * cb, long startPos, char const * data, long dsize )
{
	if( ! cb ) return ClobRC.UnexpectedNull;
	if( ! data || !dsize ) return 0;
	if( dsize < 0 ) dsize = strlen(data);
	if( (startPos + dsize) >= cb->nAlloc )
	{
		long allocsz = startPos + dsize; /* + (cb->nAlloc * 11 / 20); */
		long rc = clob_realloc( cb, allocsz );
		if( rc < ClobRC.OK ) return rc;
	}
	memcpy( cb->aData + startPos, data, dsize );
	return dsize;
}


long clob_write( Clob * cb, char const * data, long dsize )
{
	long old = cb->nCursor;
	CLOB_DUMP("clob_write()", cb );
	cb->nCursor += clob_writeat( cb, cb->nCursor, data, dsize );
	if( cb->nUsed < cb->nCursor )
	{
		cb->nUsed = cb->nCursor;
	}
	CLOB_DUMP("clob_write()", cb );
	return cb->nCursor - old;
}


long clob_append( Clob * cb, char const * data, long dsize )
{
	long old = cb->nUsed;
	cb->nUsed += clob_writeat( cb, cb->nUsed, data, dsize );
	return cb->nUsed - old;
}

long clob_append_char_n( Clob * cb, char c, long n )
{
    if( n <= 0 ) return ClobRC.RangeError;
    long rc = clob_realloc( cb, cb->nAlloc + n );
    if( rc < 0 ) return rc;
    memset( cb->aData + cb->nUsed, c, n );
    cb->nUsed += n;
    return n;
}


long clob_copy( Clob * src, Clob * dest )
{
	if( src == dest ) return ClobRC.RangeError;
	if( src->aData == dest->aData ) return ClobRC.RangeError;
	long allocsz = src->nAlloc;
	CLOB_DUMP( "copy src before",src );
	CLOB_DUMP( "copy dest before",dest );
	clob_reset( dest );
	long rc = clob_realloc( dest, allocsz );
	if( rc < ClobRC.OK ) return rc;
	dest->nUsed = src->nUsed;
	dest->nAlloc = allocsz;
	dest->nCursor = src->nCursor;
	memcpy( dest->aData, src->aData, allocsz );
	
	CLOB_DUMP( "copy src after",src );
	CLOB_DUMP( "copy dest after",dest );
	return ClobRC.OK;
}

long clob_copy_slice( Clob * src, Clob * dest, long startPos, long n )
{
	if( ! src || !dest ) return ClobRC.UnexpectedNull;
	if( n<1 ) return ClobRC.RangeError;
	if( ClobRC.OK != clob_pos_in_bounds( src, startPos ) ) return ClobRC.RangeError;
	long bpos = startPos;
	if( bpos >= src->nUsed ) return 0;
	long epos = bpos + n; /* 1-past-end marker */
	if( epos > src->nUsed ) epos = src->nUsed;
	long ret = clob_append( dest, src->aData + bpos, epos - bpos );
	return ret;
}

long clob_read( Clob * src, Clob * dest, long n )
{
	if( ! src || !dest || n<1 ) return 0;
	long bpos = src->nCursor;
	if( bpos == src->nUsed ) return 0;
	long epos = bpos + n; /* 1-past-end marker */
	if( epos > src->nUsed ) epos = src->nUsed;
	long ret = clob_append( dest, src->aData + bpos, epos - bpos );
	src->nCursor += ret;
	return ret;
}

long clob_truncate( Clob * cb, long pos, int memPolicy )
{
	if( ! cb ) return ClobRC.UnexpectedNull;
	if( cb->nUsed <= pos ) return ClobRC.OK;
	cb->nUsed = pos;
	clob_null_terminate( cb );
	long rc = ClobRC.OK;
	if( memPolicy > 0 )
	{
		rc = clob_realloc( cb, cb->nUsed );
	}
	else if( memPolicy < 0 )
	{
		/* try a simple heuristic to calculate whether a
		   realloc is worth it... */
		const long diff = (cb->nAlloc - pos);
		const int rel = 5; /* ((diff*rel) >= cb->nAlloc) = do realloc */
		const int abs = 512; /* (diff >= abs) = do realloc */
		if(
		   ( diff  > abs )
		   ||
		   ((diff * rel) >= cb->nAlloc )
		   )
		{
			rc = clob_realloc( cb, cb->nUsed );
		}
	}
	return rc;
}


long clob_memmove( Clob * cb, int start1, int n, int start2 )
{
	return clob_memmove_fill( cb, 0, start1, n, start2 );
}
long clob_memmove_fill( Clob * cb, char const filler, int start1, int n, int start2 )
{
	if( ! cb ) return ClobRC.UnexpectedNull;
	if(
	   ( (n<1) || (start2 == start1) )
	   ||
	   (ClobRC.OK != clob_pos_in_bounds(cb,start1))
	   ||
	   (ClobRC.OK != clob_pos_in_bounds(cb,start1+n))
	   ||
	   (ClobRC.OK != clob_pos_in_bounds(cb,start2))
	   ||
	   (ClobRC.OK != clob_pos_in_bounds(cb,start2+n))
	   )
	{
		return ClobRC.RangeError;
	}
	char * src = cb->aData + start1;
	char * dest = cb->aData + start2;
	long pos = 0; /* here we'll take a long "pos" ... */
	while( pos < n )
	{
		dest[start2++] = src[pos];
		src[pos++] = filler;
	}
	return n;
}

/**
   Swaps n bytes of memory contents between m1 and m2. No bounds
   checking is (or can be) done here, so m1 and m2 must both be large
   enough for the given start1, start2, and n parameters.
*/
static long memswap( char * m1,
		     long n,
		     char * m2 )
{
	char buf;
	long pos = 0;
	for( ; pos < n; ++pos )
	{
		buf = m2[pos];
		m2[pos] = m1[pos];
		m1[pos] = buf;
	}
	return pos;
}

long clob_memswap( Clob * c1, int start1, int n, Clob * c2, int start2 )
{
	if( ! c1 || ! c2 ) return ClobRC.UnexpectedNull;
	if(
	   ( (n<1) )
	   ||
	   (ClobRC.OK != clob_pos_in_bounds(c1,start1))
	   ||
	   (ClobRC.OK != clob_pos_in_bounds(c1,start1+n))
	   ||
	   (ClobRC.OK != clob_pos_in_bounds(c2,start2))
	   ||
	   (ClobRC.OK != clob_pos_in_bounds(c2,start2+n))
	   )
	{
		return ClobRC.RangeError;
	}
	return memswap( c1->aData + start1, n, c2->aData + start2 );
}


long clob_swap( Clob * c1, Clob * c2 )
{
	if( ! c1 || ! c2 ) return ClobRC.UnexpectedNull;
	Clob x = *c1;
	*c1 = *c2;
	*c2 = x;
	return ClobRC.OK;
}

long clob_clone( Clob * src, Clob ** dest )
{
	if( ! src ) return ClobRC.UnexpectedNull;
	long rc = clob_init( dest, src->aData, src->nUsed );
	if( ClobRC.OK != rc ) return rc;
	memcpy( (*dest)->aData, src->aData, src->nUsed );
	(*dest)->nUsed = src->nUsed;
	(*dest)->nCursor = src->nCursor;
	return ClobRC.OK;
}

/**
   A Clob vappendf_appender implementation. It assumes that
   arg is a (Clob *) and uses clob_append() to push
   all data to that clob.

   If s is null then 0 is returned.

   Returns the number of bytes appended.
*/
static long clob_vappendf_appender( void * arg, char const * s, long n )
{
	Clob * c = (Clob *)arg;
	if( ! c ) return 0;
	return clob_append( c, s, n );
}

long clob_vappendf( Clob * cb, char const * fmt, va_list vargs )
{
	return vappendf( clob_vappendf_appender, cb, fmt, vargs );
}

long clob_appendf( Clob * cb, const char * fmt, ... )
{
	va_list vargs;
	va_start( vargs, fmt );
	long ret = clob_vappendf( cb, fmt, vargs );
	va_end(vargs);
	return ret;
}



long clob_export( Clob const * cb, void * varg, clob_exporter pf )
{
	if( ! cb->nAlloc ) return ClobRC.UnexpectedNull;
	return pf( varg, cb->aData, cb->nUsed );
}

long clob_exporter_FILE( void * arg, char const * data, long n )
{
	FILE * fp = (FILE *) arg;
	if( ! fp ) return ClobRC.UnexpectedNull;
	return (n == fwrite( data, 1, n, fp ))
		? n
		: ClobRC.IOError;
}

long clob_export_to_FILE( Clob const * cb, FILE * dest )
{
	return clob_export( cb, dest, clob_exporter_FILE );
}

long clob_exporter_filename( void * arg, char const * data, long n )
{
	char const * fname = (char const *)arg;
	if( ! fname ) return ClobRC.UnexpectedNull;
	FILE * fp = fopen( fname, "wb" );
	if( ! fp ) return ClobRC.IOError;
	long ret = clob_exporter_FILE( fp, data, n );
	fclose( fp );
	return ret;
}

long clob_export_to_file( Clob const * cb, char const * dest )
{
    return clob_export( cb, (char *)dest, clob_exporter_filename );
    /* ^^ i hate that cast, but it's the least evil optiion here. */
}

long clob_import( Clob * dest, void * arg, clob_importer pf )
{
    return pf( dest, arg );
}

long clob_importer_FILE( Clob * dest, void * arg )
{
	FILE * fp = (FILE *) arg;
	if( ! fp ) return ClobRC.ArgError;
	long oldUsed = clob_size(dest);
	const long blocksize = 4096;
	long rdsz = 0;
	char * bcbuf[blocksize];
	int rc;
	while( ! feof(fp) )
	{
		rdsz = fread( bcbuf, sizeof(char), blocksize, fp );
		if( rdsz == 0 ) break;
		rc = clob_append( dest, (char const *)bcbuf, rdsz );
		if( rc < 0 )
		{
		    return rc;
		}
	}
	return clob_size(dest) - oldUsed;
}

long clob_importer_filename( Clob * dest, void * arg )
{
	char const * fname = (char *)arg;
	if( ! fname ) return ClobRC.ArgError;
	FILE * fh = fopen( fname, "rb" );
	if( !fh ) return ClobRC.IOError;
	long ret = clob_import( dest, fh, clob_importer_FILE );
	fclose( fh );
	return ret;
}

char * clob_vmprintf( char const * fmt, va_list vargs )
{
    Clob * buf;
    if( ClobRC.OK != clob_init(&buf,0,0) ) return 0;
    clob_vappendf( buf, fmt, vargs );
    char * ret = buf->aData;
    *buf = Clob_empty;
    clob_finalize( buf );
    return ret;
}
char * clob_mprintf( char const * fmt, ... )
{
    va_list vargs;
    va_start( vargs, fmt );
    char * ret = clob_vmprintf( fmt, vargs );
    va_end(vargs);
    return ret;
}

char * clob_take_buffer( Clob * cb )
{
    char * c = cb->aData;
    *cb = Clob_empty;
    return c;
}



#if CLOB_USE_ZLIB
//! clob_zheader_prefix == magic cookie for our compressed clob header
static char const clob_zheader_version = '1';
static char const * clob_zheader_prefix = "clob32";
static int const clob_zheader_prefix_len = 7; /* ^^^^ must be 1 + strlen(clob_zheader_prefix)! */
//! clob_zheader_lensize == number of bytes used to store size in compressed data
static short const clob_zheader_lensize = 4;
//! clob_zheader_crcsize == number of bytes used to store size in compressed data
static short const clob_zheader_crcsize = 4;
static int const clob_zheader_size = 15; /* 1 + prefix_len + lensize + clob_zheader_crcsize */

/**
   Stamps the initial header in cIn, which must be an initialized clob
   with at least clob_zheader_size bytes allocated. srcSize must be
   the decompressed size of data which will presumably be compressed
   into the cIn clob at some point and crc must be the adler32
   checksum of the uncompressed data. Returns clob_zheader_size on
   success, or a negative number on error.
*/
static int clob_write_zheader( Clob * cIn, unsigned int srcSize, unsigned long crc )
{
    if( clob_size(cIn) < clob_zheader_size )
    {
	return ClobRC.RangeError;
    }
    clob_writeat( cIn, 0, clob_zheader_prefix, clob_zheader_prefix_len );
    {
	int i = clob_zheader_prefix_len - 1;
	unsigned char * oBuf = (unsigned char *)clob_buffer(cIn);
	oBuf[i++] = clob_zheader_version;
	oBuf[i++] = srcSize>>24 & 0xff;
	oBuf[i++] = srcSize>>16 & 0xff;
	oBuf[i++] = srcSize>>8 & 0xff;
	oBuf[i++] = srcSize & 0xff;

	oBuf[i++] = crc>>24 & 0xff;
	oBuf[i++] = crc>>16 & 0xff;
	oBuf[i++] = crc>>8 & 0xff;
	oBuf[i++] = crc & 0xff;
	//MARKER; printf( "Writing zsize header at pos %d-%d: %u\n", clob_zheader_prefix_len, i, srcSize );
	MARKER; printf( "Writing zsize header: version=%c uSize=%u  adler32=%lx\n", clob_zheader_version, srcSize, crc );
    }
    return clob_zheader_size;
}

/**
Checks first bytes of cIn to see if this is data compressed
by this API. If it is, the decompressed size of the compressed
data is set in sz and the adler32 checksum of the uncompressed
data is written to crc. On success 0 or greater is returned, else
a negative value from ClobRC is returned:

- ClobRC.RangeError: cIn is too small to contain the header or
the header was written by a different API version.

- ClobRC.ArgError: header seems to be invalid, or maybe not
produced by the same version of this API.

*/
static int clob_confirm_zheader( Clob const * cIn, unsigned int * sz, unsigned long * crc )
{
    int i;
    char ver;
    if( clob_size(cIn) < clob_zheader_size )
    {
	return ClobRC.RangeError;
    }
    unsigned char const * inBuf = (unsigned char *) clob_bufferc(cIn);
    for( i = 0; i < (clob_zheader_prefix_len-1); ++i, ++inBuf )
    {
	if( *inBuf != clob_zheader_prefix[i] )
	{
	    return ClobRC.ArgError;
	}
	//MARKER; printf( "confirm header: %c\n", *inBuf );
    }
    //MARKER; printf( "confirm header: %d\n", *inBuf );
    inBuf = (unsigned const char *) clob_bufferc(cIn) + (clob_zheader_prefix_len - 1);
    ver = *inBuf;
    if( ver != clob_zheader_version )
    {
	return ClobRC.RangeError;
    }
    ++inBuf;
    //MARKER; printf( "confirm header: %d\n", *inBuf );
    *sz = (inBuf[0]<<24) + (inBuf[1]<<16) + (inBuf[2]<<8) + inBuf[3];
    *crc = (inBuf[4]<<24) + (inBuf[5]<<16) + (inBuf[6]<<8) + inBuf[7];
    //MARKER; printf( "header says zsize == %u\n", *sz );
    //MARKER; printf( "header says adler32 == %lx\n", *crc );
    return ClobRC.OK;
}


int clob_compress( Clob * cIn, Clob * cOut )
{
    const unsigned int szIn = clob_size(cIn);
    const unsigned int szOut = 13 + szIn + (szIn+999)/1000;
    unsigned long nOut;
    int rc;
    Clob * tmp;
    unsigned long adler;
    tmp = 0;
    if( cOut != cIn ) clob_reset(cOut);
    nOut = (long int) szOut;
    rc = clob_init( &tmp, 0, nOut+clob_zheader_size );
    if( 0 != rc ) return rc;

    adler = adler32(0L, Z_NULL, 0);
    adler = adler32( adler, (const Byte *) clob_bufferc(cIn), szIn );
    //MARKER; printf( "adler=%lx\n", adler );
    clob_write_zheader( tmp, szIn, adler );
    rc = compress( (unsigned char *)(clob_buffer(tmp) + clob_zheader_size),
		   &nOut,
		   (unsigned char *)clob_buffer(cIn),
		   szIn );
    if( Z_OK != rc )
    {
	clob_finalize(tmp);
	return ClobRC.IOError;
    }
    if( cOut == cIn )
    {
	clob_reset(cOut);
    }

    rc = clob_resize( tmp, nOut+clob_zheader_size );
    if( rc < (nOut+clob_zheader_size) )
    {
	clob_finalize( tmp );
	return rc;
    }
    clob_swap( cOut, tmp );
    clob_finalize( tmp );
    return ClobRC.OK;
}



int clob_uncompress(Clob *pIn, Clob *pOut)
{
    unsigned int unzSize;
    unsigned int nIn = clob_size(pIn);
    Clob * temp;
    int rc;
    unsigned long int nOut2;
    unsigned long adlerExp;
    unsigned long adlerGot;
    if( nIn<=4 )
    {
	//MARKER;
	return ClobRC.Err;
    }
    if( pOut != pIn ) clob_reset(pOut);

    rc = clob_confirm_zheader( pIn, &unzSize, &adlerExp );
    //MARKER; printf( "zsize = %u\n", unzSize );
    //MARKER; printf( "zsize = %u, adlerExp=%lx\n", unzSize, adlerExp );
    if( ClobRC.OK > rc)
    {
	//MARKER;
	return rc;
    }
    rc = clob_init(&temp,0,unzSize+1);
    if( ClobRC.OK != rc )
    {
	//MARKER;
	return rc;
    }

    {
	unsigned char *inBuf;
	inBuf = (unsigned char*) (clob_buffer(pIn) + clob_zheader_size);
	nOut2 = clob_size( temp );
	//MARKER; printf( "nOut2 = %ld\n", nOut2 );
	//MARKER; printf( "input length = %u\n", nIn - clob_zheader_size );
	//MARKER; printf( "zsize = %u\n", unzSize );
	//MARKER; printf( "zblob size = %u\n",nIn );
	rc = uncompress((unsigned char*)clob_buffer(temp), &nOut2, 
			inBuf, nIn - clob_zheader_size);
    }
    if( rc!=Z_OK )
    {
	//MARKER;
	clob_finalize(temp);
	return ClobRC.IOError;
    }
    adlerGot = adler32(0L,Z_NULL,0);
    adlerGot = adler32( adlerGot, (Bytef const *) clob_bufferc(temp), nOut2 );
    if( adlerGot != adlerExp )
    {
	//MARKER; printf( "adler32 mismatch: %lx != %lx\n", adlerExp, adlerGot );
	return ClobRC.RangeError;
    }

    rc = clob_resize(temp, nOut2);
    if( rc < nOut2 )
    {
	//MARKER;
	clob_finalize(temp);
	return rc;
    }
    clob_swap( temp, pOut );
    clob_finalize(temp);
    return ClobRC.OK;
}

int clob_deflate( Clob *cIn, Clob *cOut )
{
    const int kludge = 4;
    z_stream zS;
    unsigned int szIn = clob_size(cIn);
    unsigned int szOut = kludge + 13 + szIn + (szIn+999)/1000;
    int rc;
    Clob * tmp;
    tmp = 0;
    zS.zalloc = Z_NULL;
    zS.zfree = Z_NULL;
    zS.opaque = Z_NULL;

    if( cOut != cIn ) clob_reset(cOut);
    rc = clob_init( &tmp, 0, szOut );
    if( 0 != rc )
    {
	return rc;
    }

    {
	unsigned char * cBuf;
	cBuf = (unsigned char *) clob_buffer(tmp);
	cBuf[0] = szIn>>24 & 0xff;
	cBuf[1] = szIn>>16 & 0xff;
	cBuf[2] = szIn>>8 & 0xff;
	cBuf[3] = szIn & 0xff;

	zS.next_in = (Bytef*) clob_buffer(cIn);
	zS.avail_in = clob_size(cIn);
	zS.next_out = (Bytef*) (cBuf + kludge);
	zS.avail_out = szOut;
    }

    if( Z_OK != deflateInit( &zS, Z_DEFAULT_COMPRESSION ) )
    {
	clob_finalize(tmp);
	return ClobRC.IOError;
    }

    //printf("sizes: in=%d out=%d\n", zS.avail_in, zS.avail_out );

    rc = deflate( &zS, Z_FINISH );
    szOut = szOut - zS.avail_out;
    deflateEnd( &zS );
    //printf("deflate() rc == %d\n", rc );
    //printf("sizes: in=%d out=%d\n", zS.avail_in, zS.avail_out );
    if( Z_STREAM_END != rc )
    {
	//printf("deflate() rc == %d\n", rc );
	clob_finalize(tmp);
	return ClobRC.IOError;
    }
    if( cOut == cIn )
    {
	clob_reset(cOut);
    }
    rc = clob_resize( tmp, szOut );
    if( rc < (szOut) )
    {
	clob_finalize( tmp );
	return rc;
    }
    clob_swap( cOut, tmp );
    clob_finalize( tmp );
    return ClobRC.OK;
}


int clob_inflate(Clob *cIn, Clob *cOut)
{
    unsigned int uSize;
    unsigned int nIn = clob_size(cIn);
    Clob * temp;
    int rc;
    z_stream zS;
    const int kludge = 4;
    unsigned int infSize;
    zS.zalloc = Z_NULL;
    zS.zfree = Z_NULL;
    zS.opaque = Z_NULL;

    if( cOut != cIn ) clob_reset(cOut);
    if( nIn<=kludge )
    {
	return ClobRC.Err;
    }


    {
	unsigned char * cBuf = (unsigned char *) clob_buffer(cIn);
	uSize = (cBuf[0]<<24) + (cBuf[1]<<16) + (cBuf[2]<<8) + cBuf[3];
    }
    rc = clob_init( &temp, 0, uSize + (uSize * 10 / 100));
    if( ClobRC.OK != rc )
    {
	return rc;
    }
    //fprintf(stderr,"uSize = %u, clob_capacity(temp) = %ld\n", uSize, clob_capacity(temp) );
    zS.avail_out = clob_capacity(temp);
    zS.next_out = (Bytef*) clob_buffer(temp);
    zS.next_in = (Bytef*) (clob_buffer(cIn) + kludge);
    zS.avail_in = clob_size(cIn) - kludge;

    //fprintf(stderr,"avail_in == %d avail_out == %d\n", zS.avail_in, zS.avail_out );

    rc = inflateInit( &zS );
    if( Z_OK != rc )
    {
	//fprintf(stderr,"inflateInit() failed. rc == %d\n", rc );
	clob_finalize(temp);
	return ClobRC.IOError;
    }
    /** weird: the return from inflate() is always an error (normally
	Z_DATA_ERR) for me, but inflateEnd() succeeds and the data is
	correct.
    */
    rc = inflate( &zS, Z_FINISH );
    //fprintf(stderr,"inflate() rc == %d\n", rc );
    infSize = clob_capacity(temp) - zS.avail_out;
    //fprintf(stderr,"post: infSize == %d uSize == %d\n", infSize, uSize );
    //fprintf(stderr,"post: avail_in == %d avail_out == %d\n", zS.avail_in, zS.avail_out );
    rc = inflateEnd( &zS );
    if( rc != Z_OK ) // STREAM_END )
    {
	//fprintf(stderr, "inflate() rc == %d\n",rc);
	clob_finalize(temp);
	return ClobRC.IOError;
    }
    rc = clob_resize(temp, infSize);
    if( rc < infSize )
    {
	clob_finalize(temp);
	return rc;
    }
    if( cOut == cIn ) clob_reset(cOut);
    clob_swap( temp, cOut );
    clob_finalize(temp);
    return ClobRC.OK;
}

#endif /* CLOB_USE_ZLIB */
#undef CLOB_USE_ZLIB
#undef CLOB_DEBUG
