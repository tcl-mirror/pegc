#ifndef WANDERINGHORSE_NET_CLOB_H_INCLUDED_
#define WANDERINGHORSE_NET_CLOB_H_INCLUDED_ 1
#include <stdarg.h>
#include <stdio.h> /* FILE */
#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************
@indexpage

The clob API encapsulates behaviours related to creating, appending,
reading, writing, and freeing C-style blobs (stored in memory as char
arrays).

It is primarily intended to act as an output buffer for binary or
string data. It provides, for example, printf-like functionality which
makes use of the dynamic resize features blobs to simplify the
creation of dynamic C-style strings.

Clob is heavily inspired and influenced by code in the Fossil source
tree (http://www.fossil-scm.org) written by D. Richard Hipp. In that
tree, blobs are used to store files in memory and to provide output
buffering support for the built-in web/cgi servers.

Rather than being a fork of Dr. Hipp's code, this is a
reimplementation done solely for licensing reasons - his code is GPL
and this is Public Domain.

The underlying printf implementation was pulled from the sqlite3
source tree and is in the public domain.

Author: Stephan Beal (http://wanderinghorse.net/home/stephan/)

USAGE:

A Clob object must be initialized via clob_init() and finalized via
clob_finalize(). Between those two calls, any number of other clob-API
functions may be used (unless documented otherwise). As an example:

\code
Clob * c;
clob_init( &c, 0, 0 );
clob_appendf( c, "Hello, %s!", "world");
...
clob_finalize( c );
\endcode

You can dump a Clob to stdout with the clob_export() API:

clob_export( c, stdout, clob_exporter_FILE );

But doing so with binary data is not recommended.

TODO:

- The read/write API is not complete.



************************************************************************/

#if !defined(CLOB_USE_ZLIB)
#define CLOB_USE_ZLIB 0
#endif

/**
   ClobRC_t holds status codes for the clob API.
   Their values must be unique negative integers, with
   the exception of OK, which must be 0.

   Many functions in the clob API return 0 or a positive
   number on success and one of these codes on error.

   The are accessed like: ClobRC.AllocError. This is unconventional,
   but the intention is so that debuggers can get an actual symbol
   name.
*/
struct ClobRC_t
{
	/* The success status code. */
	const long OK;
	/* Generic error code. */
	const long Err;
	/* Signals that an allocation or reallocation failed. */
	const long AllocError;
	/* Signals that a null was passed or received where
	   one was not expected. */
	const long UnexpectedNull;
	/* Signals that some value was out of range. */
	const long RangeError;
	/** Signals an I/O error of some sort. */
	const long IOError;
	/** Signals some sort of argument-type error (e.g. (void *) args). */
	const long ArgError;
};
typedef struct ClobRC_t ClobRC_t;

/**
   The official way to get the clob status code values
   is via the ClobRC object.
*/
extern const ClobRC_t ClobRC;

/**
   The Clob type (an opaque struct) holds data relating to in-memory
   blobs allocated by malloc(). A Clob holds (and owns) a
   varying-length character array for storing arbitrary binary data.

   Note that because client code cannot see the Clob implementation,
   they cannot create Clob objects on the stack. The proper way
   to create, initialize, and free Clob objects is:

\code
   Clob * c;
   if( ClobRC.OK != clob_init(c,0,0) ) { ... error ... }
   clob_finalize( c );
\endcode
*/
typedef struct Clob Clob;

/**
   Allocates a new Clob object and assigns (*cb) to it. It initializes
   the object as described below. On error it assigns (*cb) to 0.

   It is only legal to call this on fresh, uninitialized Clob
   pointer. A clob is considered to be "uninitialized" just after
   declaration (before it is assigned to) and after clob_finalize() has
   been called on it.

   To avoid memory leaks, for each successful call to clob_init() you
   must have a matching call to clob_finalize(). If this function
   fails then (*cb) is set to 0, so there is no need to call
   clob_finalize() (but it is still safe (but a no-op) to do so, for
   the cases where doing so may simplify error handling).

   Interpretation of the 'data' and 'n' arguments:

   - If data is null and n is less than 1 then cb is initialized
   to a zero-length string.

   - If data is non-null and n is less than 0 then strlen(data) is used
   to calculate n.

   - If data is non-null and n is greater than 0 then the first n
   bytes of data are copied and data is assumed to be at least that
   long.

   - If data is 0 and n is positive then n bytes are allocated and
   zeroed.


   RETURNS:

   If this function returns ClobRC.OK then (*cb) points to a new Clob
   object. On error it returns one of the negative integers defined in
   ClobRC:

   - ClobRC.UnexpectedNull if (!cb).

   - ClobRC.AllocError if allocation of the new Clob fails.

   USAGE NOTES:

   Calling clob_finalize(0) has no ill effects (other than an
   ignorable error code), so algorithms may be organized a call
   clob_finalize(myClob) regardless of whether clob_init(&myClob)
   succeeds or fails. It is anticipated that this may simplify some
   error-handling cases.
*/
long clob_init( Clob ** cb, char const * data, long n );

/**
   A simplified form of clob_init() which allocates a new, empty Clob
   object and returns it. The caller owns it and must finalize it with
   clob_finalize().
*/
Clob * clob_new();


/**
  See clob_set_alloc_policy().
*/
typedef long (*clob_alloc_policy_t)( long );

/**
   Sets the current allocation size determination policy and returns
   the previous one (so you may politely reset it when yours goes out
   of scope).

   When the framework wants to (re)allocate memory it will call the
   current policy function and pass it the suggested amount of memory
   it wants to reallocate. The policy may return that number as-is or
   adjust it upwards (e.g. *= 1.5) (never downwards) and return that
   number, which the reallocation will then use. If a smaller number
   is returned it will be ignored.

   Note that the policy only specifies a requested size, not where the
   memory will come from. The memory will always be pulled from
   malloc(). Likewise, the cleanup routines always use free() to
   delete the memory.

   The intention is to allow clients to who know they will append
   blobs a lot to increase the size of the allocs to reduce the number
   of potential reallocations.

   Be aware that the reallocation internals may allocate slightly more
   memory than requested, e.g. to unsure that the blob always has a
   trailing null character.

   If passed 0 it will use a default policy, which simply returns the
   value passed to it. You can fetch the current policy, if you like,
   by passing 0 to this function, then passing that return value
   back to this function.
*/
clob_alloc_policy_t clob_set_alloc_policy( clob_alloc_policy_t );


/**
   Free the resources, if any, associated with cb and zeroes out the
   freed memory. After calling this, clob_buffer(cb) will be an empty
   string and both clob_size(cb) and clob_capacity(cb) will be 0.

   It is illegal to pass a Clob which was not initialized by
   clob_init(). Doing so results in undefined behaviour.

   After this function returns, cb can be re-used by any clob API
   which is append-friendly (e.g. not clob_char_filln()).

   Note that this function does not actually free the cb pointer
   itself.

   On success it returns ClobRC.OK. On error it returns:

   - ClobRC.UnexpectedNull if (!cb).
*/
long clob_reset( Clob * cb );

/**
   clob_finalize() works just like clob_reset(), plus it deallocates cb by
   calling free(). After calling this, cb is invalid until/unless it
   is passed to clob_init() again.

   Returns ClobRC.OK on success and ClobRC.UnexpectedNull if (!cb).
*/
long clob_finalize( Clob * cb );

/**
   Force's cb's cursor and used counter to be in bounds of the
   actual allocated memory. They are moved forwards or backwards,
   as necessary, to bring them into bounds.
*/
void clob_force_in_bounds( Clob * cb );

/**
   Tries to (re)allocate at least sz bytes of memory for
   the native blob associated with cb.

   If cb is "const" (points to a blob but has an allocated size of 0)
   then calling this with a non-zero sz will cause sz bytes of the
   referenced blob data to be deeply copied to cb.

   On success it returns the number of bytes allocated to cb. The
   number may be larger than sz, due to internal details of this API
   and the implementation of the current allocation policy (see
   clob_set_alloc_policy()).

   On error it returns one of the negative numbers specified
   by ClobRC:

   - ClobRC.AllocError = (re)allocation failed.

*/
long clob_realloc( Clob * cb, unsigned long sz );

/**
   Works like clob_realloc(), but marks all memory
   in the clob as used. This means that appending
   will start from sz, rather than from whatever
   the end previously was. It returns the same values
   as clob_realloc().
*/
long clob_resize( Clob * cb, unsigned long sz );

/**
   Returns the number of "used" bytes in cb.  Appending to a clob will
   start after the last "used" byte. Note that clobs may (and normally do)
   have more memory allocated to them than is "used".

   Returns 0 or greater (the size of cb) on success and
   ClobRC.UnexpectedNull if (!cb).
*/
long clob_size( Clob const * cb );

/**
   Returns the current allocated capacity of cb, or
   ClobRC.UnexpectedNull if (!cb).

   The capacity will always be equal to or greater than clob_size(cb).
*/
long clob_capacity( Clob const * cb );

/**
   Returns a pointer to cb's buffer (may be 0). The buffer is owned by
   cb.

   Do not keep ahold of the returned pointer for longer than
   necessary, as any operations which change cb may invalidate that
   pointer.

   Do not generically depend on the returned array being a
   null-terminated string, as cb can hold arbitrary data. Use
   clob_size(sb) to get the length of the data.
*/
char * clob_buffer( Clob * cb );

/**
   A variant of clob_buffer() for places where a const is needed.
*/
char const * clob_bufferc( Clob const * cb );

/**
   Gives up cb's control of any allocated memory and returns it to the
   caller, who takes over ownership.

   If a blob is storing binary data then there is no way to know the
   size of the returned buffer. Thus if you need its length, be sure
   to call clob_size(cb) before calling this.

   This function effectively does clob_reset(cb) but does not finalize
   cb. Thus after calling this function cb is still a valid object for
   purposes of the other clob_xxx() functions.
 */
char * clob_take_buffer( Clob * cb );



/**
   Flags for clob_seek(), they are identical in nature to SEEK_SET,
   SEEK_CUR, and SEEK_END used by the conventional fseek() C function.

   CLOB_SEEK_SET: moves the cursor to absolute position (offset).

   CLOB_SEEK_CUR: moves the cursor to (cursor + offset).

   CLOB_SEEK_END: moves the cursor to relative postition (end -
   offset), where 'end' is the index 1 after the last character of the
   USED portion of the blob. e.g. the end of "abcd" is at position 4.
*/
enum ClobSeekWhence { CLOB_SEEK_SET = 0,
		      CLOB_SEEK_CUR = 1,
		      CLOB_SEEK_END = -1
};

/**
   Similar to the standard fseek(), clob_seek() moves the blob's
   cursor to its (current pos + offset), relative to whence, which
   must be one of of the ClobSeekFlags values.  Once the seek is
   complete, the cursor is bumped back into bounds (or at EOF). The
   return value is the new cursor position within the blob.

   Note that when using CLOB_SEEK_END, only a negative offset makes
   sense, as a positive offset is out of bounds.

   The return value is a positive integer on success, indicting the
   new position within the blob. On error a negative number is
   returned:

   - ClobRC.RangeError = the 'whence' value is unknown.

*/
long clob_seek( Clob * cb, long offset, int whence );

/**
  Moves cb's cursor back to the begining of the blob.
*/
void clob_rewind( Clob * cb );

/** Returns ClobRC.OK if pos is within the used bounds of cb,
    else returns ClobRC.RangeError.
*/
long clob_pos_in_bounds( Clob * cb, long pos );


/**
   Fills the pointed-to memory with char specified by c, starting at
   blob offset specified by startPos and copying the next n bytes. It ignores
   the "used" space boundary and fills any allocated space. It does
   not change the size of the allocated memory block or its used-space
   count.

   On success it returns the number of zeroes which it fills
   in. Normally this is n, but if (startPos + n) would overflow, the
   blob is filled to the end and that (smaller) length is returned.

   On failure it returns one of the values defined in ClobRC:

   - ClobRC.Err if cb does not contain a blob, or points
   to a blob but does not own it.

   - ClobRC.RangeError if startPos is out of bounds or n <= 0.
*/
long clob_char_filln( Clob * cb, char c, long startPos, long n );

/**
   Same as clob_char_filln( cb, '\0', 0, clob_size( cb ) );
*/
long clob_zero_fill( Clob * cb );


/**
   Appends the first dsize bytes of data to cb's blob, exanding the
   blob as needed. If dsize is -1 then strlen(data) is used to
   calculate the length. If dsize is 0 an error code is returned (see
   below). This does not modify cb's internal position cursor.

   If cb does not currently own the blob it points to then this
   function may cause it to create a copy of that object, and append
   to that copy. This may or may not be expensive, depending on the
   size of the copy.

   Unlike clob_appendf(), this function is safe for use with binary
   data, but only if dsize is explicitly set. Otherwise it will only
   append up to the first null character in data.

   On success a positive integer is returned, the number of bytes by
   which the blob extended. On failure, one of the negative values
   defined in ClobRC is returned:

   - ClobRC.AllocError if a memory (re)allocation fails.

   - ClobRC.RangeError if dsize is 0.
*/
long clob_append( Clob * cb, char const * data, long dsize );

/**
   Appends n copies of ch to cb and returns the number added.  If n is less
   than 1 then ClobRC.RangeError is returned and cb is not modified.
*/
long clob_append_char_n( Clob * cb, char ch, const long n );

/*
  Makes a deep copy of src, placing it into dest.

  Neither src nor dest may be 0. dest is deallocated before the copy
  happens.

  Returns one of the values defined in ClobRC:

  - ClobRC.RangeError if (src==dest).

  - ClobRC.AllocError if a memory (re)allocation fails.

  - ClobRC.OK on success.
*/
long clob_copy( Clob * src, Clob * dest );



/**
   Like printf, but output is appended to the end of cb, expanding it
   as necessary. It also imlements a couple extension conversions, to
   be documented in the docs for vappendf().

   Return value: if the number is >= 0 then it is the size by which
   the target blob grew as a result of the printf (i.e. the number of
   bytes printf added to it). If the return value is negative then it
   is an error code from ClobRC.

   This function is not safe for binary data - if fmt contains
   any null characters the processing will stop.

   For the list of non-standard format specifiers, see
   the documentation for vappendf().
*/
long clob_vappendf( Clob * cb, char const * fmt, va_list vargs );


/**
   See clob_vappendf(Clob*,char const *,va_list).
*/
long clob_appendf( Clob * cb, const char * fmt, ... );



/**
   Reads up to n bytes from src, starting at startPos, and copies them
   to dest. This does not advance cb's internal cursor.

   On success the return value is the number of bytes read, which may
   be less than n (or 0 on EOF).

   On error it returns one of the negative values defined
   in ClobRC:

   - ClobRC.UnexpectedNull if src or dest are null

   - ClobRC.RangeError if n is less than 1 or if startPos is out of
   src's bounds.
*/
long clob_copy_slice( Clob * src, Clob * dest, long startPos, long n );

/**
   Reads up to n bytes from src, starting at the current
   cursor position (clob_tell()). Reading advances
   the cursor.

   The return value is the number of bytes read, which
   may be less than n (or 0 on EOF).
*/
long clob_read( Clob * src, Clob * dest, long n );

/**
   Returns the current position of cb's internal cursor, analogous to
   the standard ftell() function.
*/
long clob_tell( Clob * cb );

/**
   This function is just like clob_append() but it starts writing
   at the position returned by clob_tell(cb). Writing advances
   the internal cursor. It returns the number of bytes written.

   If dsize is -1 then strlen(data) is used for dsize.

   On success it returns the number of bytes written. On failure it
   returns a negative integer error code defined in ClobRC:

   - ClobRC.AllocationError if memory could not be allocated.

   - ClobRC.RangeError if dsize is 0.

   - ClobRC.UnexpectedNull if data is 0.
*/
long clob_write( Clob * cb, char const * data, long dsize );



/**
   This function ensures that the one-past-the-last item in the blob
   to 0.  The "used" size of cb does not change.

   On success it returns ClobRC.OK. On failure it returns one of the
   negitive integer values defined in ClobRC:

   - ClobRC.Err if cb does not own a pointer to an underlying blob.

   - ClobRC.AllocError if a memory (re)allocation fails.
*/
long clob_null_terminate( Clob * cb );

/**
   If clob_size(cb) "used space" is currently less than pos then this
   function does nothing, otherwise cb is truncated to that length.

   If allocPolicy is 0 then the amount of memory allocated by cb is
   not adjusted. If it is >0, clob_realloc() will be called to try to
   shrink the allocated buffer (but this does not guaranty that the
   allocated memory will actually be reduced). If (allocPolicy<0) then
   a simple heuristic is used to determine if a reallocation might
   release a useful amount of memory.

   Returns ClobRC.OK on success or another value from ClobRC
   on error.
*/
long clob_truncate( Clob * cb, long pos, int allocPolicy );

// TODO???: clob_trim()


/**
   clob_exporter is a generic interface to exporting Clob objects
   (e.g. to arbitrary streams).

   Policy for implementations:

   - Take a (char const *) data pointer and "export" n bytes of it,
   where the meaning of "export" is implementation-specified.

   - The 'arg' argument is an implementation-specific pointer. This API
   does not use it, but passes it on so that client code can use it, e.g.
   to accumulate or export data (e.g. by passing an output stream handle).

   - On success it must return 0 or greater. Ideally it should return the
   exact number of bytes processed, but that might not be feasible for
   some implementations. In those cases, returning n is the best approach.

   - On failure it must return a negative number, prefferably one of those
   defined by ClobRC.
*/
typedef long (*clob_exporter)( void * arg, char const * data, long n );

/**
   clob_export() is a generic interface for exporting blobs to
   "external representations".

   It calls pf( arg, ... ) one time to export cb's data using whatever
   approach pf implements. The return value is that of calling pf.
*/
long clob_export( Clob const * cb, void * arg, clob_exporter pf );

/**
   This is a sample clob_exporter for use with clob_export.
   It is used like this:

   clob_export( clob, an_open_FILE_handle, clob_exporter_FILE );

   The second argument to clob_export must be an open (FILE*).

   Returns n on success and a negative number on error.

   The file handle is not closed by this routine.
*/
long clob_exporter_FILE( void * fh, char const * data, long n );

/**
   Like clob_exporter_FILE(), but expects arg to be a (char const *)
   filename. Returns the same as the equivalent clob_exporter_FILE()
   unless arg cannot be opened as a file, in which case
   ClobRC.IOError is returned.
*/
long clob_exporter_filename( void * arg, char const * data, long n );

/**
   clob_importer is the import counterpart of clog_exporter.  It
   defines an interface for callbacks which can provide a blob with
   data.

   Note that for input it is not generically possible to
   get the length of the data before starting. Thus this
   API takes a Clob object, which it can append to, whereas
   the clob_exporter functions take raw data pointers as
   sources.

   target must be an initialized Clob. It is appended to,
   not overwritten. It is expanded as necessary.

   Return value is the number of bytes appended to the target, or a
   negative number on error (preferably one of the ClobRC error
   codes).
*/
typedef long (*clob_importer)( Clob * target, void * arg );

/**
   Returns the same as pf( arg, dest ).
*/
long clob_import( Clob * dest, void * arg, clob_importer pf );

/**
   A clob_importer implementation for clob_import. It expects
   arg to be an open (FILE*). The target is appended with the
   contents of the file. On error, a negative number is returned.
   On success, the number of bytes added to the target.

   If arg is not a (FILE*) then ClobRC.ArgError is returned.

   The file handle is not closed by this routine.
*/
long clob_importer_FILE( Clob * target, void * arg );

/**
   A clob_importer implementation similar to clob_importer_FILE(), but
   expects arg to be a (char const *) filename. Returns the same as
   the equivalent clob_importer_FILE() call unless arg cannot be
   opened as a file, in which case ClobRC.IOError is returned.
*/
long clob_importer_filename( Clob * target, void * arg );

/*
  A debugging-only function. Do not use it in client code.
*/
void clob_dump( Clob * cb, int doString );

/**
   Sends cb's blob to the given file. Returns the number
   of bytes written on success or a negative number
   (probably ClobRC.IOError) on error.

   The file handle is not closed by this routine.
 */
long clob_export_to_FILE( Clob const * cb, FILE * dest );

/**
   Identical to clob_export_to_FILE() but takes a file
   name.
*/
long clob_export_to_file( Clob const * cb, char const * dest );

/**
   Moves a block of memory within cb, starting at start1, moving n
   bytes to the position starting at start2.  All points and ranges
   must be in the clob_size() range.  start2 may be less than start1
   but they may not be equal.  A value of less than 1 for n is not
   currently supported, though it should be to support backwards
   movement of memory.

   Memory which is "moved" gets filled with the specified filler char
   after its contents are copied to its new location.

   This function does not change the size of cb.

   On success it returns the number of bytes moved (n)
   and on failure it returns:

   - ClobRC.RangeError if any values or ranges are out of bounds

   - ClobRC.UnexpectedNull if cb is null.
*/
long clob_memmove_fill( Clob * cb, char const filler, int start1, int n, int start2 );

/**
   A convenience form of clob_memmove_fill(cb,0,start1,n,start2).
*/
long clob_memmove( Clob * cb, int start1, int n, int start2 );

/**
   Swaps the contents of n bytes between two blobs. start1 is relative
   to cb1 and start2 is relative to cb2.

   On success it returns the number of bytes swapped. On error it returns
   one of:

   - ClobRC.UnexpectedNull if (!cb1) or (!cb2).

   - ClobRC.RangeError if start1 and n are not within cb1's bounds
   or start2 and n are not within cb2's bounds.

   This is a linear operation (based on the value of n). If you want
   to swap the entire contents of two clobs, you can do so in constant
   time by using clob_swap() instead of this routine.
*/
long clob_memswap( Clob * cb1, int start1, int n, Clob * cb2, int start2 );


/**
   Efficiently swaps the contents of blobs c1 and c2, which must
   both be initialized clobs.

   Returns ClobRC.OK on success or ClobRC.UnexpectedNull
   if either c1 or c2 are null.
*/
long clob_swap( Clob * c1, Clob * c2 );

/**
   Copies the contents of src to the dest, which
   must be an UNINITIALIZED Clob object.

   Returns ClobRC.OK on success or, on error:

   - ClobRC.AllocError

   - ClobRC.UnexpectedNull if src is null.

Example usage:

\code

Clob * orig;
clob_init( &orig );
clob_appendf( orig, "Hi, world." );
Clob * clone;
clob_clone( orig, &clone );

\endcode

Note the pointer-to-pointer parameter for clob_init()
and the second parameter to clob_clone().

*/
long clob_clone( Clob * src, Clob ** dest );

/**
   Works more or less like sprintf(), but supports the printf
   specifiers accepted by clob_appendf().  The caller owns the
   returned null-terminated string.
*/
char * clob_vmprintf( char const * fmt, va_list vargs );

/**
   Functionally identical to clob_vmprintf().
*/
char * clob_mprintf( char const * fmt, ... );

#if CLOB_USE_ZLIB
/**
   Compresses the contents of src to dest using the zlib compress()
   function. dest may be either the same as src or an initialized
   clob. If dest is not src then this function will clear dest's
   contents whether or not this routine succeeds. If (dest==src)
   then src is only modified if this routine succeeds.

   Returns ClobRC.OK on success, some other value on error:

   - ClobRC.IOError: compression failed

   - ClobRC.AllocError = a (re)allocation failed.

   Note that the compressed data is compressed using zlib but contains
   its own header, so it will not be usable by tools like gunzip.
*/
int clob_compress( Clob * src, Clob * dest );

/**
   The converse of clob_compress(), src is expected to be clob containing
   data compressed with clob_compress(). dest must be an initialized clob.
   If dest is not the same as src then dest will be reset whether not
   this routine succeeds. If (dest==src) then it is only modified
   if this routine succeeds.

   Returns ClobRC.OK on success, some other value on error:

   - ClobRC.RangeError: source does not appear to contain any
   compressed data or data was written by a different version of this
   API.

   - ClobRC.ArgError: input is too small to contain the compression
   header.

   - ClobRC.IOError: decompression failed

   - ClobRC.AllocError = a (re)allocation failed. 

*/
int clob_uncompress( Clob * src, Clob * dest );


/**
   Works equivalently to clob_compress(), but uses the zlib deflate()
   algorithm instead of the compress() algorithm.
*/
int clob_deflate( Clob *cIn, Clob *cOut );

/**
   The converse of clob_deflate(), this works equivalently to
   clob_uncompress(), but uses the zlib inflate() algorithm instead of
   the uncompress() algorithm.
*/
int clob_inflate( Clob *cIn, Clob *cOut );
#endif /* CLOB_USE_ZLIB */

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* WANDERINGHORSE_NET_CLOB_H_INCLUDED_ */
