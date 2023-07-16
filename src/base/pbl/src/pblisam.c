/*
 pblisam.c - isam file library implementation

 Copyright (C) 2002 - 2007   Peter Graf

 This file is part of PBL - The Program Base Library.
 PBL is free software.

    MIT License

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

 For more information on the Program Base Library or Peter Graf,
 please see: https://www.mission-base.com/.

    $Log: pblisam.c,v $
    Revision 1.26  2021/06/23 14:32:49  peter
    Switch to MIT license

    Revision 1.25  2021/06/12 11:18:26  peter

*/

/*
 * make sure "strings <exe> | grep Id | sort -u" shows the source file versions
 */
char * pblisam_c_id = "$Id: pblisam.c,v 1.26 2021/06/23 14:32:49 peter Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pbl.h"                  /* program base library                     */

/******************************************************************************/
/* #defines                                                                   */
/******************************************************************************/

/******************************************************************************/
/* typedefs                                                                   */
/******************************************************************************/

/*
 * PBL ISAM FILE DESCRIPTOR
 */
typedef struct PBLISAMFILE_s
{
    char          * magic;        /* magic string pointing to file descriptor */

    pblKeyFile_t  * mainfile;     /* file descriptor of main isam file        */
    int             update;       /* flag: file open for update               */

    int             transactions; /* number of transactions active for file   */
    int             rollback;     /* next commit should lead to a roll back    */

    int             nkeys;        /* number of key files of file              */
    pblKeyFile_t ** keyfiles;     /* file descriptors of key files            */

    int           * keydup;       /* flag array does the key allow duplicates */
    void         ** keycompare;   /* compare functions for key file           */

} PBLISAMFILE_t;

/******************************************************************************/
/* globals                                                                    */
/******************************************************************************/
static int (*pblkeycompare)( void * left, size_t llen,
                             void * right, size_t rlen );

/******************************************************************************/
/* functions                                                                  */
/******************************************************************************/

/*
 * conversion between keys of the main file and reference keys
 *
 * main file keys are 8 byte unsigned string numbers
 * between "00000000"          and "ffffffff"
 * or 17 byte unsigned string numbers
 * between "g0000000100000000" and "gffffffffffffffff"
 * this implements a 64 bit key.
 *
 * reference keys are compressed binary representations
 * of the same values.
 *
 * function pblRKey2MainKey converts from the binary reference key
 * to the unsigned string main key representing the same 64 bit value
 *
 * function pblMainKey2RKey converts from the unsigned string
 * representation to the compressed representation
 *
 * reference keys are used as data for index records of index
 * key files for non duplicate keys
 * and as key post fixes for index records of index
 * key files for keys allowing duplicates
 *
 * they provide the reference from the index records back
 * to the main file records containing the data
 */
static int pblRKey2MainKey(
unsigned char * rkey,
int             rkeylen,
unsigned char * okey
)
{
    unsigned long   keyhigh = 0;
    unsigned long   keylow = 0;
    int             hkeylen = 0;
    int             lkeylen = 0;
    int             len;

    /*
     * at least two bytes are needed, one for the length and one for
     * the value of lowkey
     */
    if( rkeylen < 2 )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * read the length of the compressed data from the end of the rkey
     */
    lkeylen = 0xff & rkey[ rkeylen - 1 ];

    /*
     * the upper half byte of the length stores the length of the highkey
     */
    hkeylen = lkeylen >> 4;

    /*
     * the lower half byte of the length stores the length of the lowkey
     */
    lkeylen &= 0x0f;

    /*
     * the length of a the low key variable string must be between 1 and 5
     */
    if( lkeylen < 1 || lkeylen > 5 )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * additional to the length byte, lkeylen bytes are needed
     */
    if( rkeylen < 1 + lkeylen )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * read the value of the low key from the end of the key
     */
    len = pbl_VarBufToLong( rkey + rkeylen - ( lkeylen + 1 ), &keylow );
    if( len != lkeylen )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * if there is no high key, just return the low key as a string
     */
    if( hkeylen < 1 )
    {
        if( okey )
        {
            pbl_LongToHexString( okey, keylow );
        }

        /*
         * return the number of bytes of the rkey parsed
         */
        return lkeylen + 1;
    }

    /*
     * the length of a the high key variable string cannot be greater than 5
     */
    if( hkeylen > 5 )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * additional to the length byte, lkeylen + hkeylen bytes are needed
     */
    if( rkeylen < 1 + lkeylen + hkeylen )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * read the value of the high key from the end of the key
     */
    len = pbl_VarBufToLong( rkey + rkeylen - (hkeylen + lkeylen + 1), &keyhigh);
    if( len != lkeylen )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * keyhigh must have a positive value, otherwise it would not have been
     * stored at all
     */
    if( !keyhigh )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * return highkey and lowkey as one string
     */
    if( okey )
    {
        *okey = 'g';
        pbl_LongToHexString( okey + 1, keyhigh );
        pbl_LongToHexString( okey + 9, keylow );
    }

    /*
     * return the number of bytes of the rkey parsed
     */
    return hkeylen + lkeylen + 1;
}

static int pblLongs2RKey(
unsigned long   keylow,
unsigned long   keyhigh,
unsigned char * rkey
)
{
    int             hkeylen = 0;
    int             lkeylen = 0;
    int             len;

    if( keyhigh )
    {
        /*
         * only store the higher four byte value if it is not 0
         */
        hkeylen = pbl_LongToVarBuf( rkey, keyhigh );
    }

    /*
     * store the low 4 bytes
     */
    lkeylen = pbl_LongToVarBuf( rkey + hkeylen, keylow );

    /*
     * store the length of both 4 bytes values in one byte at the end
     * the upper half byte of the length stores the length of the highkey
     * the lower half byte of the length stores the length of the lowkey
     */
    len = ( hkeylen << 4 ) | lkeylen;
    rkey[ hkeylen + lkeylen ] = 0xff & len;

    /*
     * return the bytes used for the rkey
     */
    return hkeylen + lkeylen + 1;
}


static int pblMainKey2RKey(
unsigned char * okey,
int             okeylen,
unsigned char * rkey
)
{
    unsigned long   keylow;
    unsigned long   keyhigh;
    int             len;

    if( okeylen > PBLKEYLENGTH - 1 )
    {
        pbl_errno = PBL_ERROR_PARAM_KEYLEN;
        return -1;
    }

    /*
     * copy the input key, because the parsing destroys it
     */
    memcpy( rkey, okey, okeylen );
    if( *rkey == 'g' )
    {
        rkey[ 17 ] = 0;
        keylow = strtoul( (char*)rkey + 9, 0, 16 );
        rkey[ 9 ] = 0;
        keyhigh = strtoul( (char*)rkey + 1, 0, 16 );
    }
    else
    {
        rkey[ 8 ] = 0;
        keylow = strtoul( (char*)rkey, 0, 16 );
        keyhigh = 0;
    }

    /*
     * store both long values as variable length byte buffers
     */
    len = pblLongs2RKey( keylow, keyhigh, rkey );

    return len;
}

/*
 * return the length a duplicate index key without the
 * reference post fix
 */
static int pblIsamDupKeyLen( unsigned char * fkey, int fkeylen )
{
    int  len;

    /*
     * parse the reference from the end of the key
     * this calculates the length of the reference needed below
     */
    len = pblRKey2MainKey( fkey, fkeylen, NULL );
    if( len < 0 )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    return fkeylen - len;
}

/*
 * compare two duplicate keys
 */
static int pblIsamDupKeyCompare(
void * left,    /** first buffer for compare               */
size_t llen,    /** length of that buffer                  */
void * right,   /** second buffer for compare              */
size_t rlen     /** length of that buffer                  */
)
{
    int  rc;

    unsigned char lkey[ PBLKEYLENGTH ];
    unsigned char rkey[ PBLKEYLENGTH ];

    int  leftlen;
    int  rightlen;

    /*
     * a buffer with a length 0 is logically smaller than any other buffer
     */
    if( !llen )
    {
        if( !rlen )
        {
            return 0;
        }
        return -1;
    }
    if( !rlen )
    {
        return 1;
    }

    leftlen  = pblRKey2MainKey( (unsigned char*)left, llen, lkey );
    rightlen = pblRKey2MainKey( (unsigned char*)right, rlen, rkey );

    if( leftlen < 1 || rightlen < 1 )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    if( leftlen >= (int)llen || rightlen >= (int)rlen )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    if( pblkeycompare )
    {
        /*
         * use the default key compare function
         */
        rc = (*pblkeycompare)( left, llen - leftlen,
                               right, rlen - rightlen );
    }
    else
    {
        /*
         * use the default key compare function
         */
        rc = pbl_memcmp( left, llen - leftlen,
                         right, rlen - rightlen );
    }

    if( !rc )
    {
        rc = strcmp( (char*)lkey, (char*)rkey );
    }

    return rc;
}

/*
------------------------------------------------------------------------------
  FUNCTION:     pblIsamStartTransOnFile

  DESCRIPTION:  start a transaction on a single ISAM file

  RESTRICTIONS: transactions can be nested

  RETURNS:      int rc == 0: the transaction was started successfully
                int rc >  0: the transaction was started
                             but another transaction has resulted in
                             a roll back request on the file already
------------------------------------------------------------------------------
*/
static int pblIsamStartTransOnFile( PBLISAMFILE_t * isam )
{
    int n;

    /*
     * if there is no transaction active for the file
     */
    if( isam->transactions < 1 )
    {
        isam->transactions = 1;
        isam->rollback = 0;
    }
    else
    {
        isam->transactions++;
    }

    /*
     * start a transaction on the main file
     */
    if( pblKfStartTransaction( isam->mainfile ) > 0 )
    {
        isam->rollback = 1;
    }

    /*
     * start transactions for all key files
     */
    for( n = 0; n < isam->nkeys; n++ )
    {
        if( pblKfStartTransaction( isam->keyfiles[ n ] ) > 0 )
        {
            isam->rollback = 1;
        }
    }

    return isam->rollback;
}


/**
 * start a transaction on a set of ISAM files
 *
 * transactions can be nested
 *
 * @return int rc == 0: the transaction was started successfully
 * @return int rc >  0: the transaction was started
 *                      but another transaction has resulted in
 *                      a roll back request on the file already
 */

int pblIsamStartTransaction(
int nfiles,                  /** number of files in ISAM file list      */
pblIsamFile_t ** isamfiles   /** ISAM file list to start transaction on */
)
{
    PBLISAMFILE_t ** files = ( PBLISAMFILE_t ** ) isamfiles;
    int n;
    int rollback = 0;

    for( n = 0; n < nfiles; n++ )
    {
        if( pblIsamStartTransOnFile( files[ n ] ) > 0 )
        {
            rollback = 1;
        }
    }

    return rollback;
}



/*
------------------------------------------------------------------------------
  FUNCTION:     pblIsamCommitFile

  DESCRIPTION:  commit or roll back changes done during a transaction
                on a single ISAM file

  RESTRICTIONS: transactions can be nested, if so the commit
                only happens when the outermost transaction
                calls a commit.

                the commit only happens to process space buffer cache,
                call pblIsamFlush() after pblIsamCommitFile() if you want to
                flush to kernel space buffer cache.

  RETURNS:      int rc == 0: the commit went OK
                int rc >  0: a roll back happened,
                             either because the caller requested it
                             or because an inner transaction
                             resulted in a roll back
                int rc <  0: an error see pbl_errno
------------------------------------------------------------------------------
*/
static int pblIsamCommitFile( PBLISAMFILE_t * isam, int rollback )
{
    int n;

    /*
     * remember if this is a roll back
     */
    if( rollback )
    {
        isam->rollback = 1;
    }
    else
    {
        /*
         * find out if any of the key files needs a roll back
         * do this by starting another transaction on the ISAM file
         * if any of the index files have a roll back isam->rollback is set!
         */
        pblIsamStartTransOnFile( isam );

        /*
         * commit the main file transaction started above
         */
        pblKfCommit( isam->mainfile, isam->rollback );
    }

    /*
     * commit the outer transaction on the main file
     */
    pblKfCommit( isam->mainfile, isam->rollback );

    /*
     * do the roll back or commit on all key files
     */
    for( n = 0; n < isam->nkeys; n++ )
    {
        if( !rollback )
        {
            /*
             * commit the transaction started above
             */
            pblKfCommit( isam->keyfiles[ n ], isam->rollback );
        }

        /*
         * commit the outer transaction
         */
        pblKfCommit( isam->keyfiles[ n ], isam->rollback );
    }

    if( !rollback )
    {
        /*
         * count the transaction started above
         */
        isam->transactions -= 1;
    }

    /*
     * we have one transaction less on the ISAM file
     */
    isam->transactions -= 1;

    return isam->rollback;
}

/**
 * commit or roll back changes done during a transaction
 *
 * transactions can be nested, if so the commit
 * only happens when the outermost transaction
 * calls a commit.
 *
 * the commit only happens to process space buffer cache,
 * call \Ref{pblIsamFlush}() after \Ref{pblIsamCommit}() if you want to
 * flush to kernel space buffer cache.
 *
 * @return int rc == 0: the commit went OK
 * @return int rc >  0: a roll back happened, either because the caller
 *                      requested it or because an inner transaction resulted
 *                      in a roll back
 * @return int rc <  0: some error, see pbl_errno
 */

int pblIsamCommit(
int nfiles,                  /** number of files in ISAM file list      */
pblIsamFile_t ** isamfiles,  /** ISAM file list to commit changes of    */
int rollback       /** != 0: roll back the changes, == 0: commit the changes  */
)
{
    PBLISAMFILE_t ** files = ( PBLISAMFILE_t ** ) isamfiles;

    int n;
    int dorollback = rollback;

    if( !rollback )
    {
        /*
         * find out if any of the files needs a roll back
         * do this by starting another transaction on the file set
         * if any of the ISAM files have a roll back dorollback is set!
         */
        if( pblIsamStartTransaction( nfiles, isamfiles ) > 0 )
        {
            dorollback = 1;
        }
    }

    /*
     * commit or roll back all files in the set
     */
    for( n = 0; n < nfiles; n++ )
    {
        if( !rollback )
        {
            /*
             * commit the transaction done above
             */
            pblIsamCommitFile( files[ n ], dorollback );
        }

        /*
         * commit the outer transaction
         */
        pblIsamCommitFile( files[ n ], dorollback );
    }

    return dorollback;
}

/**
 * open an ISAM file, creates the file if necessary
 *
 * if update is 0, the ISAM file is opened for read access only,
 * if update is not 0 the ISAM file is opened for reading and writing
 *
 * a file set tag can be attached to the ISAM file,
 * if a file having a non NULL file set tag is flushed
 * to disk all files having the same file set tag attached
 * are flushed as well.
 *
 * @return  pblIsamFile_t * retptr == NULL: an error occurred, see pbl_errno
 * @return  pblIsamFile_t * retptr != NULL: a pointer to an ISAM file descriptor
 */

pblIsamFile_t * pblIsamOpen(
char * path,         /** path of file to create                               */
int    update,       /** flag: should file be opened for update?              */
void * filesettag,   /** filesettag, for flushing multiple files consistently */
int    nkeys,        /** number of key files to create                        */
char **keyfilenames, /** list of names of key index files to create           */
int  * keydup        /** flaglist: is the i'th index key a duplicate key?     */
)
{
    char          * ptr;
    char          * keyfile;

    PBLISAMFILE_t * isam;
    int             i;

    /*
     * create the descriptor
     */
    isam = (PBLISAMFILE_t *)pbl_malloc0( "pblIsamOpen ISAMFILE", sizeof( PBLISAMFILE_t ));
    if( !isam )
    {
        return NULL;
    }

    /*
     * if the user did not specify an external file set tag
     * the isam file descriptor is used in order to make sure
     * all key files of the isam file are flushed at the same time
     */
    if( update && !filesettag )
    {
        filesettag = isam;
    }

    isam->nkeys = nkeys;
    if( isam->nkeys )
    {
        /*
         * create space for pointers to key file descriptors
         */
        isam->keyfiles = (pblKeyFile_t **)pbl_malloc0( "pblIsamOpen keyfiles",
                                      nkeys * sizeof( pblKeyFile_t * ));
        if( !isam->keyfiles )
        {
            PBL_FREE( isam );
            return NULL;
        }

        /*
         * save the duplicate key flags for all keys
         */
        isam->keydup = (int *)pbl_memdup( "pblIsamOpen keydup",
                                   keydup, nkeys * sizeof( int * ));
        if( !isam->keydup )
        {
            PBL_FREE( isam->keyfiles );
            PBL_FREE( isam );
            pbl_errno = PBL_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        /*
         * create the array of keycompare functions for all keys
         */
        isam->keycompare = (void **)pbl_malloc0( "pblIsamOpen keycompare",
                                        nkeys * sizeof( void * ));
        if( !isam->keycompare )
        {
            PBL_FREE( isam->keydup );
            PBL_FREE( isam->keyfiles );
            PBL_FREE( isam );
            pbl_errno = PBL_ERROR_OUT_OF_MEMORY;
            return NULL;
        }
    }

    /*
     * open the main file
     */
    if( update )
    {
        /*
         * try to create
         */
        isam->mainfile = pblKfCreate( path, filesettag );
    }

    if( !isam->mainfile )
    {
        /*
         * try to open
         */
        isam->mainfile = pblKfOpen( path, update, filesettag );

        /*
         * if the main file is not open
         */
        if( !isam->mainfile )
        {
            PBL_FREE( isam->keycompare );
            PBL_FREE( isam->keydup );
            PBL_FREE( isam->keyfiles );
            PBL_FREE( isam );
            return NULL;
        }
    }

    /*
     * if the name of the main file has a directory part
     * and the names of the index files do not
     * we prepend the directory part of the main file to
     * the names of the index files
     *
     * get a pointer to last / or \ in path
     */
    ptr = strrchr( path, '/' );
    keyfile = strrchr( path, '\\' );

    if( ptr )
    {
        if( keyfile > ptr )
        {
            ptr = keyfile;
        }
    }
    else
    {
        ptr = keyfile;
    }
    if( ptr )
    {
        /*
         * set pointer to the character after the slash
         */
        ptr++;
    }

    /*
     * open all key files
     */
    for( i = 0; i < nkeys; i++ )
    {
        /*
         * if the path contains a directory part
         * and the name of the key file does not
         */
        if( ptr
         && !strchr( keyfilenames[ i ], '/' )
         && !strchr( keyfilenames[ i ], '\\' ))
        {
            /*
             * build the the path to the key file
             */
            keyfile = (char*)pbl_mem2dup( "pblIsamOpen keyfile",
                                   path, ptr - path,
                                   keyfilenames[ i ],
                                   strlen( keyfilenames[ i ] ) + 1 );
        }
        else
        {
            /*
             * use key file name as given
             */
            keyfile = strdup( keyfilenames[ i ] );
        }

        if( !keyfile )
        {
            pblKfClose( isam->mainfile );
            PBL_FREE( isam->keycompare );
            PBL_FREE( isam->keydup );
            PBL_FREE( isam->keyfiles );
            PBL_FREE( isam );
            pbl_errno = PBL_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        if( update )
        {
            /*
             * try create
             */
            isam->keyfiles[ i ] = pblKfCreate( keyfile, filesettag );
        }

        if( !isam->keyfiles[ i ] )
        {
            /*
             * try open
             */
            isam->keyfiles[ i ] = pblKfOpen( keyfile, update, filesettag );
        }

        if( !isam->keyfiles[ i ] )
        {
            int j;

            for( j = 0; j < i; j++ )
            {
                pblKfClose( isam->keyfiles[ j ] );
            }

            pblKfClose( isam->mainfile );
            PBL_FREE( keyfile );
            PBL_FREE( isam->keycompare );
            PBL_FREE( isam->keydup );
            PBL_FREE( isam->keyfiles );
            PBL_FREE( isam );
            return NULL;
        }

        /*
         * set our custom compare function for key files allowing
         * duplicate keys
         */
        if( isam->keydup[ i ] )
        {
            pblKfSetCompareFunction( isam->keyfiles[ i ], pblIsamDupKeyCompare);
        }

        /*
         * the key file is open, we don't need its name anymore
         */
        PBL_FREE( keyfile );
    }

    isam->magic = pblisam_c_id;
    return (pblIsamFile_t *)isam;
}

/**
 * close an ISAM file
 *
 * all changes are flushed to disk before,
 * all memory allocated for the file is released.
 *
 * @return int rc == 0: call went OK, file is closed
 * @return int rc != 0: some error, see pbl_errno
 */

int pblIsamClose(
pblIsamFile_t * isamfile           /** ISAM file to close */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    int             rc = 0;
    int             i;
    int             saveerrno = 0;

    /*
     * close all the key files
     */
    for( i = 0; i < isam->nkeys; i++ )
    {
        if( pblKfClose( isam->keyfiles[ i ] ))
        {
            saveerrno = pbl_errno;
            rc = -1;
        }
    }

    /*
     * close the main file
     */
    if( pblKfClose( isam->mainfile ))
    {
        saveerrno = pbl_errno;
        rc = -1;
    }

    PBL_FREE( isam->keycompare );
    PBL_FREE( isam->keydup );
    PBL_FREE( isam->keyfiles );
    PBL_FREE( isam );

    if( rc )
    {
        pbl_errno = saveerrno;
    }

    return rc;
}

/**
 * flush an ISAM file
 *
 * all changes are flushed to disk,
 *
 * @return int rc == 0: call went OK
 * @return int rc != 0: some error, see pbl_errno
 */

int pblIsamFlush(
pblIsamFile_t * isamfile           /** ISAM file to flush */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    int             rc = 0;
    int             i;
    int             saveerrno = 0;

    /*
     * flush all the key files
     */
    for( i = 0; i < isam->nkeys; i++ )
    {
        if( pblKfFlush( isam->keyfiles[ i ] ))
        {
            saveerrno = pbl_errno;
            rc = -1;
        }
    }

    /*
     * flush the main file
     */
    if( pblKfFlush( isam->mainfile ))
    {
        saveerrno = pbl_errno;
        rc = -1;
    }

    if( rc )
    {
        pbl_errno = saveerrno;
    }

    return rc;
}

/*
 * set the current record of the main file
 * used after deletes of records in the main file
 */
static int pblIsamSetCurrentRecord( PBLISAMFILE_t * isam )
{
    long          datalen;
    unsigned char okey[ PBLKEYLENGTH ];
    size_t        okeylen;
    int           saveerrno = pbl_errno;

    /*
     * read the key of the current record in the main file
     */
    datalen = pblKfThis( isam->mainfile, okey, &okeylen );
    if( datalen >= 0 )
    {
        if( okeylen == 9 )
        {
            okeylen = 8;
        }
        else if( okeylen == 18 )
        {
            okeylen = 17;
        }
        okey[ okeylen ] = 0;

        /*
         * position the current record of the main file to the allkeys record
         */
        datalen = pblKfFind( isam->mainfile, PBLFI, okey, okeylen, 0, 0 );
        if( datalen < 0 )
        {
            pbl_errno = PBL_ERROR_BAD_FILE;
            return -1;
        }
    }

    pbl_errno = saveerrno;
    return 0;
}

/**
 * insert a new record with the given keys and data into the isam file,
 *
 * the current record of the file will be set to the new record
 *
 * <P>
 * <B>RESTRICTIONS</B>:
 * <BR> - the file must be open for update,
 * <BR> - allkeys must point to the keys to be inserted,
 * <BR> - allkeyslen must be bigger than 0 and smaller than 1024,
 * <BR> - data must point to the data be inserted,
 * <BR> - datalen must not be negative,
 * <BR> - if datalen == 0, the pointer data is not evaluated at all
 *
 * Parameter <I>allkeys</I> must contain all values for all keys
 * of the record. The values have to be prepended by one byte giving
 * the length of the following value. All values have to be concatenated
 * into one string.
 *
 * Example:
 * <PRE> 4isam4file3key </PRE>
 * with the numbers as binary values and the letters ASCII,
 * specifies three keys with the values "isam", "file" and "key".
 *
 * @return int rc == 0: call went OK
 * @return int rc != 0: some error occurred, see pbl_errno
 */

int pblIsamInsert(
pblIsamFile_t * isamfile,   /** ISAM file to insert to                    */
void          * allkeys,    /** pointers to all keys to insert            */
size_t          allkeyslen, /** total length of all keys to insert        */
void          * data,       /** data to insert                            */
size_t          datalen     /** length of the data                        */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    unsigned long   lastkeyhigh = 0;
    unsigned long   lastkeylow = 0;
    unsigned char   rkey[ PBLKEYLENGTH ];
    int             rkeylen;
    unsigned char   okey[ PBLKEYLENGTH ];
    size_t          okeylen;
    int             ndatarecords = 0;
    int             n = 0;
    int             rc;
    unsigned char * ldata;
    long            ldatalen;
    unsigned char * key;
    int             keylen;

    /*
     * start a transaction
     */
    pblIsamStartTransaction( 1, &isamfile );

    /*
     * the sum of the length of all keys
     * cannot be longer than a single data record
     */
    if( allkeyslen > PBLDATALENGTH )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_PARAM_KEYLEN;
        return -1;
    }

    /*
     * find out the last key used in the main file
     */
    if( pblKfLast( isam->mainfile, okey, &okeylen ) < 0 )
    {
        if( pbl_errno != PBL_ERROR_NOT_FOUND )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            return -1;
        }

        /*
         * 00000000 is the smallest key we use in the main file
         */
#ifdef PBL_MS_VS_2012
#pragma warning(disable: 4996)
#endif
        strcpy( (char*)okey, "00000000" );
        okeylen = 8;
    }

    /*
     * generate the next key, a 64 bit logic is used in order to make
     * sure we never run out of new keys
     */
    if( *okey == 'g' )
    {
        /*
         * values over unsigned 0xffffffff are represented
         * as 17 byte strings with a preceding "g"
         */
        okey[ 17 ] = 0;
        lastkeylow = strtoul( (char*)okey + 9, 0, 16 );
        okey[ 9 ] = 0;
        lastkeyhigh = strtoul( (char*)okey + 1, 0, 16 );
    }
    else
    {
        /*
         * values below unsigned 0xffffffff are represented
         * as 8 byte strings with no prefix
         */
        okey[ 8 ] = 0;
        lastkeylow = strtoul( (char*)okey, 0, 16 );
    }

    if( lastkeylow == 0xffffffff )
    {
        /*
         * 32 bit overflow
         */
        lastkeylow = 0;
        lastkeyhigh += 1;
    }
    else
    {
        lastkeylow += 1;
    }

    if( lastkeyhigh )
    {
        snprintf( (char*)okey, PBLKEYLENGTH, "g%08lx%08lx", lastkeyhigh, lastkeylow );
    }
    else
    {
        snprintf( (char*)okey, PBLKEYLENGTH, "%08lx", lastkeylow );
    }
    okeylen = strlen( (char*)okey );

    /*
     * create the reference key used as link to the main file
     */
    rkeylen = pblMainKey2RKey( okey, okeylen, rkey );
    if( rkeylen < 1 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }

    /*
     * insert all the keys into the key files
     */
    for( key = (unsigned char*)allkeys, n = 0; n < isam->nkeys; n++ )
    {
        keylen = 0xff & *key++;
        if( keylen < 1 )
        {
            /*
             * non duplicate keys cannot be empty
             */
            if( !isam->keydup[ n ] )
            {
                /*
                 * roll back all changes
                 */
                pblIsamCommit( 1, &isamfile, 1 );

                pbl_errno = PBL_ERROR_PARAM_KEY;
                return -1;
            }
        }

        /*
         * check the sanity of the allkeys record given
         */
        if( key + keylen > ((unsigned char*)allkeys) + allkeyslen )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );

            pbl_errno = PBL_ERROR_PARAM_KEY;
            return -1;
        }

        /*
         * if the key is allowing duplicates
         */
        if( isam->keydup[ n ] )
        {
            unsigned char ikey[ PBLKEYLENGTH ];

            /*
             * create a unique key for the insert
             */
            if( keylen + rkeylen > PBLKEYLENGTH )
            {
                /*
                 * roll back all changes
                 */
                pblIsamCommit( 1, &isamfile, 1 );

                pbl_errno = PBL_ERROR_PARAM_KEYLEN;
                return -1;
            }

            /*
             * concatenate the key and the reference key
             * the reference key is used as post fix of the index key
             */
            if( keylen )
            {
                memcpy( ikey, key, keylen );
            }
            memcpy( ikey + keylen, rkey, rkeylen );

            /*
             * make sure any user defined key compare function gets used
             */
            pblkeycompare = (int(*)(void*, size_t, void*, size_t))isam->keycompare[ n ];

            /*
             * search for the key in the file
             */
            if( pblKfFind( isam->keyfiles[ n ],
                           PBLEQ, ikey, keylen + rkeylen, 0, 0 ) >= 0 )
            {
                /*
                 * roll back all changes
                 */
                pblIsamCommit( 1, &isamfile, 1 );

                pbl_errno = PBL_ERROR_EXISTS;
                return -1;
            }

            /*
             * insert the key to the file
             */
            if( pblKfInsert( isam->keyfiles[ n ],
                             ikey, keylen + rkeylen, 0, 0 ))
            {
                int saveerrno = pbl_errno;

                /*
                 * roll back all changes
                 */
                pblIsamCommit( 1, &isamfile, 1 );

                pbl_errno = saveerrno;
                return -1;
            }
        }
        else
        {
            /*
             * search for the key in the file
             */
            if( pblKfFind( isam->keyfiles[ n ],
                           PBLEQ, key, keylen, 0, 0 ) >= 0 )
            {
                /*
                 * roll back all changes
                 */
                pblIsamCommit( 1, &isamfile, 1 );

                pbl_errno = PBL_ERROR_EXISTS;
                return -1;
            }

            /*
             * insert the key to the file
             * the reference key is the data of the record in the index
             */
            if( pblKfInsert( isam->keyfiles[ n ],
                             key, keylen, rkey, rkeylen ))
            {
                int saveerrno = pbl_errno;

                /*
                 * roll back all changes
                 */
                pblIsamCommit( 1, &isamfile, 1 );

                pbl_errno = saveerrno;
                return -1;
            }
        }

        /*
         * move the key along the record
         */
        key += keylen;
    }

    /*
     * insert the data records into the main file
     */
    for( ldata = (unsigned char*)data, ndatarecords = 0; ; ndatarecords++ )
    {
        ldatalen = datalen - ndatarecords * PBLDATALENGTH;
        if( ldatalen < 0 )
        {
            break;
        }

        if( ldatalen > PBLDATALENGTH )
        {
            ldatalen = PBLDATALENGTH;
        }

        /*
         * the data records in the main file use a 9/18 byte
         * version of the main key string, including the trailing \0
         * we use the trailing \0 of the main key in order to differentiate
         * between the allkeys record and the data records
         */
        if( pblKfInsert( isam->mainfile, okey, okeylen + 1, ldata, ldatalen ))
        {
            int saveerrno = pbl_errno;

            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );

            pbl_errno = saveerrno;
            return -1;
        }

        if( ldatalen < PBLDATALENGTH )
        {
            break;
        }

        ldata += ldatalen;
    }

    /*
     * insert the record for the keys into the main file
     * the "allkeys" record in the main file uses the 8/17 byte main key
     */
    rc = pblKfInsert( isam->mainfile, okey, okeylen, allkeys, allkeyslen );
    if( rc )
    {
        int saveerrno = pbl_errno;

        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );

        pbl_errno = saveerrno;
        return -1;
    }

    /*
     * commit all changes
     */
    if( pblIsamCommit( 1, &isamfile, 0 ))
    {
        return -1;
    }

    return 0;
}


/**
 * insert a new record with the given keys and data into the isam file,
 *
 * the current record of the file will be set to the new record
 *
 * <P>
 * <B>RESTRICTIONS</B>:
 * <BR> - the file must be open for update,
 * <BR> - argc must be the number of arguments in argv,
 * <BR> - argv must be a NULL terminated argument sequence,
 * <BR> - data must point to the data be inserted,
 * <BR> - datalen must not be negative,
 * <BR> - if datalen == 0, the pointer data is not evaluated at all
 *
 * Parameters <I>argc</I> and <I>argv</I> are similar to the C main
 * function arguments and must contain all values for all keys
 * of the record.
 *
 * @return int rc == 0: call went OK
 * @return int rc != 0: some error occurred, see pbl_errno
 */

int pblIsamInsertArgcArgv(
pblIsamFile_t * isamfile,   /** ISAM file to insert to                    */
int             argc,       /** number of arguments in argv               */
char          * argv[],     /** pointers to all keys to insert            */
void          * data,       /** data to insert                            */
size_t          datalen     /** length of the data                        */
)
{
    size_t allkeyslen = 0;
    char   allkeys[ PBLDATALENGTH + 1 ];
    int    i;

    for( i = 0; i < argc; i++ )
    {
        size_t keylen = strlen( argv[ i ] );
        if( keylen > PBLKEYLENGTH )
        {
            snprintf( pbl_errstr, PBL_ERRSTR_LEN,
                      "pblIsamInsertArgcArgv: strlen() of the key with index %d is %d bytes, only %d bytes are allowed.\n",
                      i, (int)keylen, PBLKEYLENGTH );
            pbl_errno = PBL_ERROR_PARAM_KEYLEN;
            return -1;
        }

        if( allkeyslen + 1 > PBLDATALENGTH )
        {
            snprintf( pbl_errstr, PBL_ERRSTR_LEN,
                      "pblIsamInsertArgcArgv: the length of all keys up to index %d is %d bytes, only %d bytes are allowed.\n",
                      i, (int)allkeyslen + 1, PBLDATALENGTH );
            pbl_errno = PBL_ERROR_PARAM_KEYLEN;
            return -1;
        }
        allkeys[ allkeyslen++ ] = (char) keylen;

        if( allkeyslen + keylen > PBLDATALENGTH )
        {
            snprintf( pbl_errstr, PBL_ERRSTR_LEN,
                      "pblIsamInsertArgcArgv: the length of all keys up to index %d is %d bytes, only %d bytes are allowed.\n",
                      i, (int)( allkeyslen + keylen ), PBLDATALENGTH );
            pbl_errno = PBL_ERROR_PARAM_KEYLEN;
            return -1;
        }
        memcpy( allkeys + allkeyslen, argv[ i ], keylen );
        allkeyslen += keylen;
    }

    return pblIsamInsert( isamfile, allkeys, allkeyslen, data, datalen );
}

/**
 * delete the current record of the ISAM file.
 *
 * the current record of the file is set to the next record or
 * if the last record is deleted, to the previous record,
 *
 * if there are no more records in the file after the delete
 * the current record is of course unpositioned
 *
 * <P>
 * <B>RESTRICTIONS</B>:
 * <BR> - the file must be open for update,
 *
 * @return int rc == 0: call went OK
 * @return int rc != 0: some error occurred, see pbl_errno
 */

int pblIsamDelete(
pblIsamFile_t * isamfile    /** ISAM file to delete from                  */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    unsigned char   okey[ PBLKEYLENGTH ];
    size_t          okeylen;
    unsigned char   rkey[ PBLKEYLENGTH ];
    int             rkeylen = -1;
    unsigned char * key;
    int             keylen;
    unsigned char   data[ PBLDATALENGTH ];
    long            datalen;
    long            rc;
    int             n;
    int             retval = 0;

    /*
     * start a transaction
     */
    pblIsamStartTransaction( 1, &isamfile );

    /*
     * read the key of the current record in the main file
     */
    datalen = pblKfThis( isam->mainfile, okey, &okeylen );
    if( datalen < 0 )
    {
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }

    /*
     * length 8 or 17 is for allkeys records
     * if the length is 9 or 18, the record is a data record
     */
    if( okeylen != 8 && okeylen != 17 )
    {
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_POSITION;
        return -1;
    }
    okey[ okeylen ] = 0;

    /*
     * the allkeys record can not be longer than a single data record
     */
    if( datalen > PBLDATALENGTH )
    {
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * read all the keys
     */
    rc = pblKfRead( isam->mainfile, data, datalen );
    if( rc < 0 )
    {
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }
    else if( rc != datalen )
    {
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * delete all the keys from the key files
     */
    for( key = data, n = 0; n < isam->nkeys; n++ )
    {
        keylen = 0xff & *key++;

        /*
         * check the sanity of the allkeys record given
         */
        if( key + keylen > data + datalen )
        {
            pbl_errno = PBL_ERROR_BAD_FILE;
            retval = -1;
            continue;
        }

        /*
         * if the key is allowing duplicates
         */
        if( isam->keydup[ n ] )
        {
            unsigned char ikey[ PBLKEYLENGTH ];

            /*
             * a non unique key is deleted, we need the reference key
             */
            if( rkeylen < 0 )
            {
                rkeylen = pblMainKey2RKey( okey, okeylen, rkey );
            }
            if( rkeylen < 0 )
            {
                pbl_errno = PBL_ERROR_BAD_FILE;
                retval = -1;
                continue;
            }

            /*
             * the key in the index record has the reference key
             * as a post fix appended to it
             *
             * create a unique key for the delete
             */
            if( keylen + rkeylen > PBLKEYLENGTH )
            {
                pbl_errno = PBL_ERROR_BAD_FILE;
                retval = -1;
                continue;
            }

            /*
             * concatenate the key and the reference
             */
            memcpy( ikey, key, keylen );
            memcpy( ikey + keylen, rkey, rkeylen );

            /*
             * make sure any user defined key compare function gets used
             */
            pblkeycompare = (int(*)(void*, size_t, void*, size_t))isam->keycompare[ n ];

            /*
             * delete the key from the file
             */
            if( pblKfFind( isam->keyfiles[ n ],
                           PBLEQ, ikey, keylen + rkeylen, 0, 0 ) < 0 )
            {
                pbl_errno = PBL_ERROR_BAD_FILE;
                retval = -1;
                continue;
            }

            if( pblKfDelete( isam->keyfiles[ n ] ))
            {
                pbl_errno = PBL_ERROR_BAD_FILE;
                retval = -1;
                continue;
            }
        }
        else
        {
            /*
             * directly use the key as stored in the allkeys record
             * of the main file
             *
             * delete the key from the index file
             */
            if( pblKfFind( isam->keyfiles[ n ],
                           PBLEQ, key, keylen, 0, 0 ) < 0 )
            {
                pbl_errno = PBL_ERROR_BAD_FILE;
                retval = -1;
                continue;
            }

            if( pblKfDelete( isam->keyfiles[ n ] ))
            {
                pbl_errno = PBL_ERROR_BAD_FILE;
                retval = -1;
                continue;
            }
        }

        /*
         * move the key along the keys
         */
        key += keylen;
    }

    /*
     * delete all records from the main file
     * this deletes the "allkeys" record having a key length of 8/17 bytes
     */
    while( pblKfFind( isam->mainfile, PBLEQ, okey, okeylen, 0, 0 ) >= 0 )
    {
        if( pblKfDelete( isam->mainfile ))
        {
            pbl_errno = PBL_ERROR_BAD_FILE;
            retval = -1;
        }
    }

    /*
     * this deletes the data records having a key length of 9/18 bytes
     */
    while( pblKfFind( isam->mainfile, PBLEQ, okey, okeylen + 1, 0, 0 ) >= 0 )
    {
        if( pblKfDelete( isam->mainfile ))
        {
            pbl_errno = PBL_ERROR_BAD_FILE;
            retval = -1;
        }
    }

    /*
     * position the current record of the main file to the
     * allkeys record of another entry of the main file
     */
    pblIsamSetCurrentRecord( isam );

    if( retval < 0 )
    {
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }

    if( pblIsamCommit( 1, &isamfile, 0 ))
    {
        return -1;
    }
    return retval;
}

/*
 * get the main key from a given index key
 *
 * for non duplicate index keys this assumes
 * that the index key file is positioned
 * on the record who's key is given as skey
 *
 * for index keys allowing duplicates this assumes
 * that the skey given is the "long" version,
 * with the reference key post fix attached.
 *
 * the current record of the main file is
 * positioned on the allkeys record having the key
 */
static int pblIsamGetMainKey(
PBLISAMFILE_t  * isam,
int              index,
void           * skey,
size_t           skeylen,
void           * okey
)
{
    long            rc;
    unsigned char   key[ PBLKEYLENGTH ];
    int             keylen;
    long            datalen;

    /*
     * make sure the index is in bounds
     */
    if( index >= isam->nkeys )
    {
        pbl_errno = PBL_ERROR_PARAM_INDEX;
        return -1;
    }

    /*
     * if the key has duplicates
     */
    if( isam->keydup[ index ] )
    {
        /*
         * the main key is a post fix of the reference key
         * read the key from there and convert it to an unsigned string
         */
        keylen = pblRKey2MainKey( (unsigned char*)skey, skeylen, (unsigned char*)okey );
        if( keylen < 0 )
        {
            return -1;
        }
    }
    else
    {
        /*
         * read the reference, this assumes that the record is positioned!
         */
        rc = pblKfRead( isam->keyfiles[ index ], key, sizeof( key ) );
        if( rc < 0 )
        {
            return -1;
        }
        keylen = rc;

        /*
         * get the key used in the main file
         */
        if( pblRKey2MainKey( key, keylen, (unsigned char*)okey ) < 0 )
        {
            return -1;
        }
    }

    /*
     * get the length of the key
     */
    keylen = strlen( (char*)okey );

    /*
     * position the current record of the main file to the allkeys record
     */
    datalen = pblKfFind( isam->mainfile, PBLFI, okey, keylen, 0, 0 );
    if( datalen < 0 )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    return keylen;
}


/*
 * find for index keys with possible duplicates
 *
 * writes the index key found with the reference post fix attached
 * to the supplied buffer rkey
 *
 * returns the length of that key excluding the reference post fix
 *
 * sets the length of that key including the reference post fix to rkeylen
 */
static int pblIsamFindDupKey(
PBLISAMFILE_t  * isam,
int              which,
int              index,
unsigned char  * skey,
size_t           skeylen,
unsigned char  * rkey,
size_t         * rkeylen
)
{
    long            datalen;
    unsigned char   fkey[ PBLKEYLENGTH ];
    size_t          fkeylen = 0;
    unsigned char   ikey[ PBLKEYLENGTH ];
    size_t          ikeylen = 0;
    int             keylen;
    int             lwhich = PBLLT;
    int             rc;

    /*
     * the search key needs to leave space for the reference
     */
    if( skeylen > PBLKEYLENGTH - 2 )
    {
        pbl_errno = PBL_ERROR_PARAM_KEYLEN;
        return -1;
    }

    switch( which )
    {
      case PBLLT:
        /*
         * create a reference key smaller than all real ones
         */
        fkeylen = pblLongs2RKey( 0, 0, fkey );

        /*
         * search for key lower than the one created
         */
        lwhich = PBLLT;
        break;

      case PBLLE:

        /*
         * the lower equal case is treated as FIRST or LOWER THAN
         */
        rc = pblIsamFindDupKey( isam, PBLFI, index,
                                skey, skeylen, rkey, rkeylen );
        if( rc > 0 )
        {
            return rc;
        }

        rc = pblIsamFindDupKey( isam, PBLLT, index,
                                     skey, skeylen, rkey, rkeylen );
        return rc;
        break;

      case PBLFI:
      case PBLEQ:
        /*
         * create a reference key smaller than all real ones
         */
        fkeylen = pblLongs2RKey( 0, 0, fkey );

        /*
         * search for key greater than the one created
         */
        lwhich = PBLGT;
        break;

      case PBLLA:
        /*
         * create a reference key bigger than all real ones
         */
        fkeylen = pblLongs2RKey( 0xffffffff, 0xffffffff, fkey );

        /*
         * search for a key lower than the one created
         */
        lwhich = PBLLT;
        break;

      case PBLGE:
        /*
         * the lower equal case is treated as LAST or GREATER THAN
         */
        rc = pblIsamFindDupKey( isam, PBLLA, index,
                                     skey, skeylen, rkey, rkeylen );
        if( rc > 0 )
        {
            return rc;
        }

        rc = pblIsamFindDupKey( isam, PBLGT, index,
                                skey, skeylen, rkey, rkeylen );
        return rc;
        break;

      default: /* PBLGT */
        /*
         * create a reference key bigger than all real ones
         */
        fkeylen = pblLongs2RKey( 0xffffffff, 0xffffffff, fkey );

        /*
         * search for a key greater than the one created
         */
        lwhich = PBLGT;
        break;
    }

    /*
     * create the key for the search
     */
    if( fkeylen + skeylen >= PBLKEYLENGTH )
    {
        pbl_errno = PBL_ERROR_PARAM_KEYLEN;
        return -1;
    }

    /*
     * concatenate the key and the reference key
     * the reference key is used as post fix of the index key
     */
    if( skeylen )
    {
        memcpy( ikey, skey, skeylen );
    }
    memcpy( ikey + skeylen, fkey, fkeylen );
    ikeylen = skeylen + fkeylen;

    /*
     * find the record in the key file
     */
    datalen = pblKfFind( isam->keyfiles[ index ], lwhich,
                         ikey, ikeylen, fkey, &fkeylen );
    if( datalen < 0 )
    {
        return -1;
    }

    /*
     * calculate the length of the key without the reference
     */
    keylen = pblIsamDupKeyLen( fkey, fkeylen );
    if( keylen < 0 )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * in the FIRST, EQUAL and the LAST case the key has to match
     */
    if( which == PBLFI || which == PBLEQ || which == PBLLA )
    {
        /*
         * see whether the key matches the search key
         */
        if( skeylen != (size_t)keylen || memcmp( skey, fkey, skeylen ))
        {
            pbl_errno = PBL_ERROR_NOT_FOUND;
            return -1;
        }
    }

    /*
     * save the key including the reference as return value
     */
    memcpy( rkey, fkey, fkeylen );
    *rkeylen = fkeylen;

    return keylen;
}


/**
 * Find a record in an ISAM file, set the current record.
 *
 * Parameter 'which' specifies which record to find relative
 * to the search key specified by 'skey' and 'skeylen'.
 * The following values for 'which' are possible
 *
 * <BR><B> PBLEQ </B> - find a record whose key is equal to 'skey'
 * <BR><B> PBLFI </B> - find the first record that is equal
 * <BR><B> PBLLA </B> - find the last record that is equal
 * <BR><B> PBLGE </B> - find the last record that is equal or the smallest
 *                      record that is greater
 * <BR><B> PBLGT </B> - find the smallest record that is greater
 * <BR><B> PBLLE </B> - find the first record that is equal or the biggest
 *                      record that is smaller
 * <BR><B> PBLLT </B> - find the biggest record that is smaller
 *
 * Parameter 'index' specifies which of the keys to use.
 * The pseudo index value -1 can be used in order to
 * search for technical keys in the main file.
 * If the pseudo index value of -1 is used only the
 * values PBLEQ, PBLFI and PBLLA are allowed for 'which'.
 *
 * <P>
 * <B>RESTRICTIONS</B>:
 * <BR> - The out parameter 'okey' must point to a memory area that is
 *        big enough to hold any possible key, i.e 255 bytes
 *
 * @return int rc >= 0:
 * <UL>
 * <LI>                  Call went OK,
 *                       the value returned is the length
 *                       of the key of the record found,
 * <LI>                  The key of the record is copied to 'okey',
 * <LI>                  The current record of the file is set to the
 *                       record found
 * </UL>
 *
 * @return int rc <  0:
 * <UL>
 * <LI>                  Some error occurred, see pbl_errno,
 *                       Especially PBL_ERROR_NOT_FOUND, if there is no
 *                       matching record
 * </UL>
 */

int pblIsamFind(
pblIsamFile_t  * isamfile,  /** ISAM file to search in                    */
int              which,     /** mode to use for search                    */
int              index,     /** index of key to use for search            */
void           * skey,      /** key to use for search                     */
size_t           skeylen,   /** length of search key                      */
void           * okey       /** buffer for result key                     */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    long            datalen;
    unsigned char   key[ PBLKEYLENGTH ];
    int             keylen;
    size_t          okeylen = 0;

    /*
     * make sure the index is in bounds
     */
    if( index != -1 && index >= isam->nkeys )
    {
        pbl_errno = PBL_ERROR_PARAM_INDEX;
        return -1;
    }

    if( index == -1)
    {
    	if( which != PBLEQ && which != PBLFI && which != PBLLA )
        {
            pbl_errno = PBL_ERROR_PARAM_INDEX;
            return -1;
        }

        /*
         * find the record in the main file
         */
        datalen = pblKfFind( isam->mainfile, which,
                             skey, skeylen, okey, &okeylen );
        if( datalen < 0 )
        {
            return datalen;
        }
        return okeylen;
    }

    /*
     * if the key has duplicates
     */
    if( isam->keydup[ index ] )
    {
        /*
         * make sure any user defined key compare function gets used
         */
        pblkeycompare = (int(*)(void*, size_t, void*, size_t))isam->keycompare[ index ];

        /*
         * search the duplicate key
         */
        keylen = pblIsamFindDupKey( isam, which, index,
                                    (unsigned char*)skey, skeylen, (unsigned char*)okey, &okeylen );
        if( keylen < 0 )
        {
            return keylen;
        }
    }
    else
    {
        /*
         * find the record in the key file
         */
        datalen = pblKfFind( isam->keyfiles[ index ], which,
                             skey, skeylen, okey, &okeylen );
        if( datalen < 0 )
        {
            return datalen;
        }

        if( datalen < 2 || datalen > PBLKEYLENGTH )
        {
            pbl_errno = PBL_ERROR_BAD_FILE;
            return -1;
        }

        /*
         * set the return value
         */
        keylen = okeylen;
    }

    /*
     * position current record of the main file on that key
     */
    if( pblIsamGetMainKey( isam, index, okey, okeylen, key ) < 1 )
    {
        return -1;
    }

    if( isam->keydup[ index ] )
    {
        ((char*)okey)[ keylen ] = 0;
    }

    return keylen;
}

/*
 * read a key from the allkeys record of an entry in the main file
 *
 * if the reference is requested it is appended to the key
 */
static int pblIsamThisKey(
PBLISAMFILE_t  * isam,
int              index,
int              reference,
unsigned char  * okey
)
{
    int             okeylen = -1;
    unsigned char   rkey[ PBLKEYLENGTH ];
    int             rkeylen;
    unsigned char   fkey[ PBLKEYLENGTH ];
    size_t          fkeylen;
    unsigned char   data[ PBLDATALENGTH ];
    long            datalen;
    long            rc;
    unsigned char * key;
    int             n;

    /*
     * make sure the index is in bounds
     */
    if( index >= isam->nkeys )
    {
        pbl_errno = PBL_ERROR_PARAM_INDEX;
        return -1;
    }

    /*
     * read the key of the current record in the main file
     */
    datalen = pblKfThis( isam->mainfile, fkey, &fkeylen );
    if( datalen < 0 )
    {
        return -1;
    }

    /*
     * length 8 or 17 is for allkeys records
     * if the length is 9 or 18, the record is a data record
     */
    if( fkeylen != 8 && fkeylen != 17 )
    {
        pbl_errno = PBL_ERROR_POSITION;
        return -1;
    }
    fkey[ fkeylen ] = 0;

    /*
     * the allkeys record can not be longer than a single data record
     */
    if( datalen > PBLDATALENGTH )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * read all the keys
     */
    rc = pblKfRead( isam->mainfile, data, datalen );
    if( rc < 0 )
    {
        return -1;
    }
    else if( rc != datalen )
    {
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * get the key
     */
    for( key = data, n = 0; n <= index; n++ )
    {
        okeylen = 0xff & *key++;

        if( key + okeylen > data + datalen )
        {
            pbl_errno = PBL_ERROR_BAD_FILE;
            return -1;
        }

        if( n < index )
        {
            /*
             * move the key along the keys
             */
            key += okeylen;
            continue;
        }

        memcpy( okey, key, okeylen );

        /*
         * if the reference of a duplicate key is requested
         */
        if( reference && isam->keydup[ n ] )
        {
            /*
             * create the reference post fix for the key
             */
            rkeylen = pblMainKey2RKey( fkey, fkeylen, rkey );
            if( rkeylen < 0 )
            {
                pbl_errno = PBL_ERROR_BAD_FILE;
                return -1;
            }

            /*
             * concatenate the key and the reference
             */
            if( okeylen + rkeylen > PBLKEYLENGTH )
            {
                pbl_errno = PBL_ERROR_BAD_FILE;
                return -1;
            }

            memcpy( okey + okeylen, rkey, rkeylen );
            okeylen += rkeylen;
        }
    }

    return okeylen;
}

/**
 * Get the key and key length of a record
 *
 * Parameter 'which' specifies which record to get relative
 * to the search key specified by index.
 * The following values for 'which' are possible
 *
 * <BR><B> PBLTHIS  </B> - get key and keylen of current record
 * <BR><B> PBLNEXT  </B> - get key and keylen of next record
 * <BR><B> PBLPREV  </B> - get key and keylen of previous record
 * <BR><B> PBLFIRST </B> - get key and keylen of first record
 * <BR><B> PBLLAST  </B> - get key and keylen of last record
 *
 * Parameter 'index' specifies which of the keys to get,
 * the pseudo index value -1 can be used in order to access the file
 * sequentially in the order the records were inserted.
 * The output parameter 'okey' is set to the technical key in the main
 * file in this case, the key length of that technical key is returned
 * in case the call went OK.
 *
 * <P>
 * <B>RESTRICTIONS</B>:
 * <BR> - The out parameter 'okey' must point to a memory area that is
 *        big enough to hold any possible key, i.e 255 bytes
 *
 * @return int rc >= 0:
 * <UL>
 * <LI>                  Call went OK,
 *                       The value returned is the length
 *                       of the key of the record found,
 * <LI>                  The key of the record is copied to 'okey',
 * <LI>                  The current record of the file is set to the
 *                       record found.
 * </UL>
 *
 * @return int rc <  0:
 * <UL>
 * <LI>                  Some error occurred, see pbl_errno.
 * </UL>
 */

int pblIsamGet(
pblIsamFile_t  * isamfile,  /** ISAM file to read in                      */
int              which,     /** mode to use for read                      */
int              index,     /** index of key to use for read              */
void           * okey       /** buffer for result key                     */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    unsigned char   rkey[ PBLKEYLENGTH ];
    size_t          okeylen = 0;
    long            datalen;
    int             rc;

    /*
     * make sure the index is in bounds
     */
    if(( index != -1 ) && ( index >= isam->nkeys ))
    {
        pbl_errno = PBL_ERROR_PARAM_INDEX;
        return -1;
    }

    if( index >= 0 && isam->keydup[ index ] )
    {
        /*
         * make sure any user defined key compare function gets used
         */
        pblkeycompare = (int(*)(void*, size_t, void*, size_t))isam->keycompare[ index ];
    }

    switch( which )
    {
      case PBLNEXT:
        if( index == -1 )
        {
            /*
             * save the current position of the main file
             */
            if( pblKfSavePosition( isam->mainfile ))
            {
                return -1;
            }
        }

        while( index == -1 )
        {
            /*
             * read the next entry in the main file
             */
            datalen = pblKfNext( isam->mainfile, okey, &okeylen );
            if( datalen < 0 )
            {
                pblKfRestorePosition( isam->mainfile );
                return -1;
            }

            /*
             * length 8 or 17 is for allkeys records
             * if the length is 9 or 18, the record is a data record
             */
            if( okeylen != 8 && okeylen != 17 )
            {
                /*
                 * found a data record, continue reading records
                 */
                continue;
            }

            return okeylen;
        }

        /*
         * position to next record in the index file
         */
        datalen = pblKfNext( isam->keyfiles[ index ], okey, &okeylen );
        if( datalen < 0 )
        {
            return -1;
        }
        break;

      case PBLPREV:
        if( index == -1 )
        {
            /*
             * save the current position of the main file
             */
            if( pblKfSavePosition( isam->mainfile ))
            {
                return -1;
            }
        }

        while( index == -1 )
        {
            /*
             * read the previous entry in the main file
             */
            datalen = pblKfPrev( isam->mainfile, okey, &okeylen );
            if( datalen < 0 )
            {
                pblKfRestorePosition( isam->mainfile );
                return -1;
            }

            /*
             * length 8 or 17 is for allkeys records
             * if the length is 9 or 18, the record is a data record
             */
            if( okeylen != 8 && okeylen != 17 )
            {
                /*
                 * found a data record, continue reading records
                 */
                continue;
            }

            return okeylen;
        }

        /*
         * position to previous record in the index file
         */
        datalen = pblKfPrev( isam->keyfiles[ index ], okey, &okeylen );
        if( datalen < 0 )
        {
            return -1;
        }
        break;

      case PBLFIRST:
        if( index == -1 )
        {
            datalen = pblKfFirst( isam->mainfile, okey, &okeylen );
            if( datalen < 0 )
            {
                return -1;
            }
            return okeylen;
        }

        datalen = pblKfFirst( isam->keyfiles[ index ], okey, &okeylen );
        if( datalen < 0 )
        {
            return -1;
        }
        break;

      case PBLLAST:
        if( index == -1 )
        {
            datalen = pblKfLast( isam->mainfile, okey, &okeylen );
            if( datalen < 0 )
            {
                return -1;
            }
            if( okeylen == 8 || okeylen == 17 )
            {
                return 0;
            }
        }

        while( index == -1 )
        {
            /*
             * read the previous entry in the main file
             */
            datalen = pblKfPrev( isam->mainfile, okey, &okeylen );
            if( datalen < 0 )
            {
                return -1;
            }

            /*
             * length 8 or 17 is for allkeys records
             * if the length is 9 or 18, the record is a data record
             */
            if( okeylen != 8 && okeylen != 17 )
            {
                /*
                 * found a data record, continue reading records
                 */
                continue;
            }

            return okeylen;
        }

        datalen = pblKfLast( isam->keyfiles[ index ], okey, &okeylen );
        if( datalen < 0 )
        {
            return -1;
        }
        break;

      case PBLTHIS:
        if( index == -1 )
        {
            datalen = pblKfThis( isam->mainfile, okey, &okeylen );
            if( datalen < 0 )
            {
                return -1;
            }
            return okeylen;
        }

        /*
         * read the key with the reference
         */
        rc = pblIsamThisKey( isam, index, 1, (unsigned char*)okey );
        if( rc <= 0 )
        {
            return rc;
        }
        okeylen = rc;

        /*
         * find the record in the key file
         * position the current record in the index file
         */
        datalen = pblKfFind( isam->keyfiles[ index ],
                             PBLEQ, okey, okeylen, 0, 0 );
        if( datalen < 0 )
        {
            return -1;
        }
        break;

      default:
        pbl_errno = PBL_ERROR_PARAM_MODE;
        return -1;
    }

    /*
     * no need to position the current record of the main file in this case
     */
    if( which != PBLTHIS )
    {
        /*
         * position the current record of the main file
         */
        rc = pblIsamGetMainKey( isam, index, okey, okeylen, rkey );
        if( rc < 0 )
        {
            return -1;
        }
    }

    /*
     * if the key has duplicates
     */
    if( isam->keydup[ index ] )
    {
        /*
         * calculate the length of the key without the reference post fix
         */
        rc = pblIsamDupKeyLen( (unsigned char*)okey, okeylen );
        if( rc < 0 )
        {
            return -1;
        }
        okeylen = rc;

        ((char*)okey)[ okeylen ] = 0;
    }

    return okeylen;
}

/**
 * read the key and keylen of the current record
 *
 * parameter index specifies which of the keys to read
 *
 * <P>
 * <B>RESTRICTIONS</B>:
 * <BR> - the out parameter okey must point to a memory area that is
 *        big enough to hold any possible key, i.e 255 bytes
 *
 * @return int rc >= 0:
 * <UL>
 * <LI>                  call went OK,
 *                       the value returned is the length of the key
 * <LI>                  the key of the record is copied to okey,
 * <LI>                  the current record of the file is not affected
 *                       by this function
 * </UL>
 * @return int rc <  0:
 * <UL>
 * <LI>                  some error occurred, see pbl_errno
 * </UL>
 */

int pblIsamReadKey(
pblIsamFile_t  * isamfile,  /** ISAM file to read in                      */
int              index,     /** index of key read                         */
void           * okey       /** buffer for result key                     */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    int             okeylen;

    /*
     * make sure the index is in bounds
     */
    if( index >= isam->nkeys )
    {
        pbl_errno = PBL_ERROR_PARAM_INDEX;
        return -1;
    }

    /*
     * read the key without the reference
     */
    okeylen = pblIsamThisKey( isam, index, 0, (unsigned char*)okey );

    return okeylen;
}

/**
 * read the datalen of the current record
 *
 * @return long rc >= 0: call went OK, the value returned is the length
 *                       of the data of the record,
 * @return long rc <  0: some error occurred, see pbl_errno
 */

long pblIsamReadDatalen(
pblIsamFile_t * isamfile   /** ISAM file to read length of data from */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    unsigned char   rkey[ PBLKEYLENGTH ];
    size_t          rkeylen;
    unsigned char   fkey[ PBLKEYLENGTH ];
    size_t          fkeylen;
    long            datalen = 0;
    long            rc = 0;

    /*
     * read the key of the current record in the main file
     */
    rc = pblKfThis( isam->mainfile, fkey, &fkeylen );
    if( rc < 0 )
    {
        return -1;
    }

    /*
     * length 8 or 17 is for allkeys records
     * if the length is 9 or 18, the record is a data record
     */
    if( fkeylen != 8 && fkeylen != 17 )
    {
        pbl_errno = PBL_ERROR_POSITION;
        return -1;
    }
    fkey[ fkeylen ] = 0;

    /*
     * save the current position of the main file
     */
    if( pblKfSavePosition( isam->mainfile ))
    {
        return -1;
    }

    /*
     * calculate the data length of the record
     */
    for( ;; )
    {
        rc = pblKfNext( isam->mainfile, rkey, &rkeylen );
        if( rc < 0 )
        {
            if( pbl_errno == PBL_ERROR_NOT_FOUND )
            {
                pbl_errno = 0;
                break;
            }
            datalen = -1;
            break;
        }

        /*
         * if we are now positioned on an entry with a different main key
         */
        if(( fkeylen != rkeylen - 1 ) || memcmp( fkey, rkey, fkeylen ))
        {
            break;
        }

        datalen += rc;
    }

    /*
     * restore the current position of the main file
     */
    if( pblKfRestorePosition( isam->mainfile ))
    {
        return -1;
    }

    return datalen;
}

/**
 * read the data of the current record
 *
 * parameter bufferlen specifies how many bytes to read
 *
 * @return long rc >= 0: call went OK, the value returned is the length
 *                       of the data copied
 * @return long rc <  0: some error occurred, see pbl_errno
 */

long pblIsamReadData(
pblIsamFile_t * isamfile,  /** ISAM file to read from                      */
void          * buffer,    /** buffer to read to                           */
size_t          bufferlen  /** length of that buffer                       */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    unsigned char   rkey[ PBLKEYLENGTH ];
    size_t          rkeylen;
    unsigned char   fkey[ PBLKEYLENGTH ];
    size_t          fkeylen;
    long            dataread = 0;
    long            rc;

    /*
     * read the key of the current record in the main file
     */
    rc = pblKfThis( isam->mainfile, fkey, &fkeylen );
    if( rc < 0 )
    {
        return -1;
    }

    /*
     * length 8 or 17 is for allkeys records
     * if the length is 9 or 18, the record is a data record
     */
    if( fkeylen != 8 && fkeylen != 17 )
    {
        pbl_errno = PBL_ERROR_POSITION;
        return -1;
    }
    fkey[ fkeylen ] = 0;

    /*
     * save the current position of the main file
     */
    if( pblKfSavePosition( isam->mainfile ))
    {
        return -1;
    }

    /*
     * read the data of the record
     */
    while( bufferlen > 0 )
    {
        rc = pblKfNext( isam->mainfile, rkey, &rkeylen );
        if( rc < 0 )
        {
            if( pbl_errno == PBL_ERROR_NOT_FOUND )
            {
                pbl_errno = 0;
                break;
            }
            dataread = -1;
            break;
        }

        /*
         * if we are now positioned on an entry with a different main key
         */
        if(( fkeylen != rkeylen - 1 ) || memcmp( fkey, rkey, fkeylen ))
        {
            break;
        }

        if( rc > (long)bufferlen )
        {
            rc = bufferlen;
        }

        if( rc > 0 )
        {
            rc = pblKfRead( isam->mainfile, ((char*)buffer) + dataread, rc );
            if( rc < 0 )
            {
                dataread = -1;
                break;
            }
            bufferlen -= rc;
            dataread  += rc;
        }
    }

    /*
     * restore the current position of the main file
     */
    if( pblKfRestorePosition( isam->mainfile ))
    {
        return -1;
    }

    return dataread;
}

/**
 * update the data of the current record
 *
 * parameter datalen specifies how many bytes to write
 *
 * the file must be open for update
 *
 * @return long rc >= 0: call went OK, the value returned is the length
 *                       of the data copied
 * @return long rc <  0: some error occurred, see pbl_errno
 */

long pblIsamUpdateData(
pblIsamFile_t * isamfile,  /** ISAM file to update                       */
void          * data,      /** data to write                             */
size_t          datalen    /** length of that data                       */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    unsigned char   rkey[ PBLKEYLENGTH ];
    size_t          rkeylen;
    unsigned char   fkey[ PBLKEYLENGTH ];
    size_t          fkeylen;
    long            nwritten = 0;
    long            n;
    long            rc;
    long            olddatalen;

    /*
     * start a transaction
     */
    pblIsamStartTransaction( 1, &isamfile );

    /*
     * read the key of the current record in the main file
     */
    rc = pblKfThis( isam->mainfile, fkey, &fkeylen );
    if( rc < 0 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }

    /*
     * read the old datalen of the records
     */
    olddatalen = pblIsamReadDatalen( isamfile );

    /*
     * length 8 or 17 is for allkeys records
     * if the length is 9 or 18, the record is a data record
     */
    if( fkeylen != 8 && fkeylen != 17 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_POSITION;
        return -1;
    }
    fkey[ fkeylen ] = 0;

    /*
     * when the datalen gets shorter we first delete records
     */
    while( (long)datalen < olddatalen )
    {
        /*
         * position to the last record that has data
         */
        rc = pblKfFind( isam->mainfile, PBLLA, fkey, fkeylen + 1, 0, 0 );
        if( rc < 0 )
        {
            break;
        }

        /*
         * we shorten the data of the record
         */
        olddatalen -= rc;

        /*
         * delete the record
         */
        rc = pblKfDelete( isam->mainfile );
        if( rc < 0 )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );

            return -1;
        }

        if( (long)datalen < olddatalen )
        {
            /*
             * we need to delete more records from the end
             */
            continue;
        }

        /*
         * position the current record of the main file to the allkeys record
         */
        rc = pblKfFind( isam->mainfile, PBLFI, fkey, fkeylen, 0, 0 );
        if( rc < 0 )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );

            pbl_errno = PBL_ERROR_BAD_FILE;
            return -1;
        }
        break;
    }

    /*
     * update existing records
     */
    while( datalen > 0 )
    {
        rc = pblKfNext( isam->mainfile, rkey, &rkeylen );
        if( rc < 0 )
        {
            if( pbl_errno == PBL_ERROR_NOT_FOUND )
            {
                pbl_errno = 0;
                break;
            }
            nwritten = -1;
            break;
        }

        /*
         * if we are now positioned on an entry with a different main key
         */
        if(( fkeylen != rkeylen - 1 ) || memcmp( fkey, rkey, fkeylen ))
        {
            break;
        }

        n = datalen;
        if( n > PBLDATALENGTH )
        {
            n = PBLDATALENGTH;
        }

        /*
         * update the data of the record
         */
        if( n > 0 )
        {
            rc = pblKfUpdate( isam->mainfile, ((char*)data) + nwritten, n );
            if( rc < 0 )
            {
                nwritten = -1;
                break;
            }
            datalen -= n;
            nwritten  += n;
        }
    }

    /*
     * append new records
     */
    while( datalen > 0 && nwritten >= 0 )
    {
        n = datalen;
        if( n > PBLDATALENGTH )
        {
            n = PBLDATALENGTH;
        }

        if( n > 0 )
        {
            rc = pblKfInsert( isam->mainfile, fkey, fkeylen + 1,
                              ((char*)data) + nwritten, n );
            if( rc < 0 )
            {
                nwritten = -1;
                break;
            }
            datalen -= n;
            nwritten  += n;
        }
    }

    /*
     * position the current record of the main file to the allkeys record
     */
    rc = pblKfFind( isam->mainfile, PBLFI, fkey, fkeylen, 0, 0 );
    if( rc < 0 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );

        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    if( nwritten < 0 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }

    /*
     * commit all changes
     */
    if( pblIsamCommit( 1, &isamfile, 0 ))
    {
        return -1;
    }

    return nwritten;
}

/**
 * update a key of the current record of the ISAM file
 *
 * parameter index specifies which of the keys to update
 *
 * the file must be open for update
 *
 * @return int rc == 0: call went OK
 * @return int rc != 0: some error occurred, see pbl_errno
 */

int pblIsamUpdateKey(
pblIsamFile_t  * isamfile,   /** ISAM file to update key for                */
int              index,      /** index of key to update                     */
void           * ukey,       /** new value for the key to update            */
size_t           ukeylen     /** length of that value                       */
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;
    unsigned char   okey[ PBLKEYLENGTH ];
    size_t          okeylen;
    unsigned char   rkey[ PBLKEYLENGTH ];
    int             rkeylen = -1;
    unsigned char   fkey[ PBLKEYLENGTH ];
    int             fkeylen;
    unsigned char * key;
    size_t          keylen;
    unsigned char   data[ PBLDATALENGTH ];
    long            datalen;
    long            rc;
    int             n;
    unsigned char   newdata[ 2 * PBLDATALENGTH ];
    long            newdatalen = 0;
    unsigned char * newkey = newdata;

    /*
     * start a transaction
     */
    pblIsamStartTransaction( 1, &isamfile );

    /*
     * position the current record of the key file in question
     */
    fkeylen = pblIsamGet( isamfile, PBLTHIS, index, fkey );
    if( fkeylen < 0 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }

    /*
     * read the key of the current record in the main file
     */
    datalen = pblKfThis( isam->mainfile, okey, &okeylen );
    if( datalen < 0 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }

    /*
     * length 8 or 17 is for allkeys records
     * if the length is 9 or 18, the record is a data record
     */
    if( okeylen != 8 && okeylen != 17 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_POSITION;
        return -1;
    }
    okey[ okeylen ] = 0;

    /*
     * the allkeys record can not be longer than a single data record
     */
    if( datalen > PBLDATALENGTH )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * read all the keys
     */
    rc = pblKfRead( isam->mainfile, data, datalen );
    if( rc < 0 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }
    else if( rc != datalen )
    {
        /*
         *  all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_BAD_FILE;
        return -1;
    }

    /*
     * make sure the key given is OK
     */
    if(( ukeylen > PBLKEYLENGTH ) || ( ukeylen < 1 && !isam->keydup[ index ] ))
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        pbl_errno = PBL_ERROR_PARAM_KEYLEN;
        return -1;
    }

    /*
     * copy all the keys, update the new one
     */
    for( key = data, n = 0; n < isam->nkeys; n++ )
    {
        if( n == index )
        {
            keylen = 0xff & *key++;
            if( keylen == ukeylen && !memcmp( key, ukey, ukeylen ))
            {
                /*
                 * the key has not changed
                 */
                pblIsamCommit( 1, &isamfile, 0 );
                return 0;
            }

            /*
             * copy the new key into the allkeys array
             */
            *newkey++ = 0xff & ukeylen;
            if( 0xff & ukeylen )
            {
                memcpy( newkey, ukey, 0xff & ukeylen );
                newkey += 0xff & ukeylen;
            }
            key += keylen;
        }
        else
        {
            keylen = 0xff & *key++;
            *newkey++ = (char) keylen;

            /*
             * copy the old key
             */
            if( keylen )
            {
                memcpy( newkey, key, keylen );
                newkey += keylen;
                key    += keylen;
            }
        }

        /*
         * check whether the new allkeys array is in bounds
         */
        if( newkey - newdata > PBLDATALENGTH )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            pbl_errno = PBL_ERROR_PARAM_KEYLEN;
            return -1;
        }
    }
    newdatalen = newkey - newdata;

    /*
     * update the entry in the index file
     * if the key is allowing duplicates
     */
    if( isam->keydup[ index ] )
    {
        unsigned char ikey[ PBLKEYLENGTH ];

        /*
         * create the reference key
         */
        rkeylen = pblMainKey2RKey( okey, okeylen, rkey );
        if( rkeylen < 0 )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            pbl_errno = PBL_ERROR_BAD_FILE;
            return -1;
        }

        /*
         * create a unique key for the insert
         */
        if( ukeylen + rkeylen > PBLKEYLENGTH )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            pbl_errno = PBL_ERROR_PARAM_KEYLEN;
            return -1;
        }

        /*
         * concatenate the key and the reference
         */
        memcpy( ikey, ukey, ukeylen );
        memcpy( ikey + ukeylen, rkey, rkeylen );

        /*
         * make sure any user defined key compare function gets used
         */
        pblkeycompare = (int(*)(void*, size_t, void*, size_t))isam->keycompare[ index ];

        /*
         * search for the new key in the file
         */
        if( pblKfFind( isam->keyfiles[ index ],
                       PBLEQ, ikey, ukeylen + rkeylen, 0, 0 ) >= 0 )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            pbl_errno = PBL_ERROR_EXISTS;
            return -1;
        }

        /*
         * delete the old record
         */
        if( pblKfDelete( isam->keyfiles[ index ] ))
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            return -1;
        }

        /*
         * insert the key to the file
         */
        if( pblKfInsert( isam->keyfiles[ index ],
                         ikey, ukeylen + rkeylen,
                         0, 0 ))
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            return -1;
        }
    }
    else
    {
        /*
         * search for the new key in the file
         */
        if( pblKfFind( isam->keyfiles[ index ],
                       PBLEQ, ukey, ukeylen, 0, 0 ) >= 0 )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            pbl_errno = PBL_ERROR_EXISTS;
            return -1;
        }

        /*
         * create the reference key
         */
        rkeylen = pblMainKey2RKey( okey, okeylen, rkey );
        if( rkeylen < 0 )
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            pbl_errno = PBL_ERROR_BAD_FILE;
            return -1;
        }

        /*
         * delete the old record
         */
        if( pblKfDelete( isam->keyfiles[ index ] ))
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            return -1;
        }

        /*
         * insert the key to the file
         */
        if( pblKfInsert( isam->keyfiles[ index ],
                         ukey, ukeylen,
                         rkey, rkeylen ))
        {
            /*
             * roll back all changes
             */
            pblIsamCommit( 1, &isamfile, 1 );
            return -1;
        }
    }

    /*
     * update the allkeys record
     */
    rc = pblKfUpdate( isam->mainfile, newdata, newdatalen );
    if( rc < 0 )
    {
        /*
         * roll back all changes
         */
        pblIsamCommit( 1, &isamfile, 1 );
        return -1;
    }

    /*
     * commit all changes
     */
    if( pblIsamCommit( 1, &isamfile, 0 ))
    {
        return -1;
    }

    return rc;
}

/**
 * set an application specific compare function for a key of an ISAM file
 *
 * parameter index specifies which of the indices to set
 * the compare function for
 *
 * an application specific compare function can be used in order to
 * implement special orderings of the values of an index, e.g.
 * because of the use of European "umlauts" in names
 *
 * the default compare function is the c-library memcmp function
 * the key compare function should behave like memcmp
 *
 * @return int rc == 0: call went OK
 * @return int rc != 0: some error occurred, see pbl_errno
 */

int pblIsamSetCompareFunction(
pblIsamFile_t  * isamfile,    /** ISAM file to set function for         */
int              index,       /** index of key to set function for      */
int ( *keycompare )           /** compare function to set               */
    (
                void* left,   /** "left" buffer for compare             */
                size_t llen,  /** length of that buffer                 */
                void* right,  /** "right" buffer for compare            */
                size_t rlen   /** length of that buffer                 */
    )
)
{
    PBLISAMFILE_t * isam = ( PBLISAMFILE_t * ) isamfile;

    /*
     * make sure the index is in bounds
     */
    if( index >= isam->nkeys )
    {
        pbl_errno = PBL_ERROR_PARAM_INDEX;
        return -1;
    }

    /*
     * if the key has duplicates
     */
    if( isam->keydup[ index ] )
    {
        /*
         * we need to remember the compare function in the ISAM layer
         * for use in the pblIsamDupKeyCompare function
         */
        isam->keycompare[ index ] = keycompare;
    }
    else
    {
        /*
         * set the custom compare function to the KF layer
         */
        pblKfSetCompareFunction( isam->keyfiles[ index ], keycompare );

    }

    return 0;
}

