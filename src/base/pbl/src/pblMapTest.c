/*
 pblMapTest.c - map test frame

 Copyright (C) 2010   Peter Graf

 This file is part of PBL - The Program Base Library.
 PBL is free software.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 For more information on the Program Base Library or Peter Graf,
 please see: http://www.mission-base.com/.

 $Log: pblMapTest.c,v $
 Revision 1.10  2018/03/10 18:00:45  peter
 Cleanup of unneeded parentheses

 Revision 1.9  2016/10/13 01:52:05  peter
 Cleanup of the map ...Str() methods

 Revision 1.8  2010/11/07 01:21:28  peter
 Cleanup.

 Revision 1.7  2010/10/21 21:04:07  peter
 Removed warnings Eclipse CDT port to Win 7.

 Revision 1.6  2010/08/20 20:10:25  peter
 Implemented the priority queue functions.

 Revision 1.5  2010/08/14 20:10:30  peter
 Cleanup during linux tests of pblPriorityQueue interface.

 Revision 1.2  2010/05/20 21:42:53  peter
 Added pblSetReplace.

 Revision 1.1  2010/05/19 22:38:45  peter
 Testing the map.

 */

/*
 * Make sure "strings <exe> | grep Id | sort -u" shows the source file versions
 */
char* pblMapTest_c_id =
        "$Id: pblMapTest.c,v 1.10 2018/03/10 18:00:45 peter Exp $";

#include <stdio.h>
#include <memory.h>

#ifndef __APPLE__
#include <malloc.h>
#endif

#include "pbl.h"

/*****************************************************************************/
/* Functions                                                                 */
/*****************************************************************************/

/*
 * Test frame for the map library
 *
 * this test frame calls the map library,
 * it does not have any parameters, it is meant for
 * debugging the map library
 */
int pblMap_TestFrame( int argc, char * argv[ ] )
{
    PblMap * map;
    int rc;

    char * data;

    map = pblMapNewTreeMap();
    fprintf( stdout, "pblMapNewTreeMap() map = %p\n", map );

    rc = pblMapIsEmpty( map );
    fprintf( stdout, "pblMapIsEmpty( map ) rc = %d\n", rc );

    rc = pblMapAddStrStr( map, "123", "123_1" );
    fprintf( stdout, "pblMapAddStrStr( map, 123, 123_1 ) rc = %d\n", rc );

    rc = pblMapContainsKeyStr( map, "123" );
    fprintf( stdout, "pblMapContainsKeyStr( map, 123 ) rc = %d\n", rc );

    rc = pblMapAddStrStr( map, "123", "123_2" );
    fprintf( stdout, "pblMapAddStrStr( map, 123, 123_2 ) rc = %d\n", rc );

    rc = pblMapContainsKeyStr( map, "123" );
    fprintf( stdout, "pblMapContainsKeyStr( map, 123 ) rc = %d\n", rc );

    rc = pblMapAddStrStr( map, "124", "124" );
    fprintf( stdout, "pblMapAddStrStr( map, 124, 124 ) rc = %d\n", rc );

    rc = pblMapContainsKeyStr( map, "124" );
    fprintf( stdout, "pblMapContainsKeyStr( map, 124 ) rc = %d\n", rc );

    rc = pblMapContainsKeyStr( map, "not there" );
    fprintf( stdout, "pblMapContainsKeyStr( map, not there ) rc = %d\n", rc );

    rc = pblMapContainsValueStr( map, "124" );
    fprintf( stdout, "pblMapContainsValueStr( map, 124 ) rc = %d\n", rc );

    rc = pblMapContainsValueStr( map, "not there" );
    fprintf( stdout, "pblMapContainsValueStr( map, not there ) rc = %d\n", rc );

    data = (char*)pblMapGetStr( map, "123" );
    fprintf( stdout, "pblMapGetStr( map, 123 ) data = %s\n",
             data );

    data = (char*)pblMapGetStr( map, "124" );
    fprintf( stdout, "pblMapGetStr( map, 124 ) data = %s\n",
    		data );

    data = (char*)pblMapGetStr( map, "not there" );
    fprintf( stdout,
             "pblMapGetStr( map, not there ) data = %p\n",
             data );

    data = (char*)pblMapPutStrStr( map, "123", "123_3" );
    fprintf(
             stdout,
             "pblMapPutStrStr( map, 123, 123_3 ) data = %s\n",
             data );
    PBL_FREE(data);

    data = (char*)pblMapGetStr( map, "123" );
    fprintf( stdout, "pblMapGetStr( map, 123 ) data = %s\n",
    		data );

    data = (char*)pblMapPutStrStr( map, "125", "125" );
    fprintf( stdout,
             "pblMapPutStrStr( map, 125, 125 ) data = %p\n",
             data );
    PBL_FREE(data);

    data = (char*)pblMapRemoveStr( map, "125" );
    fprintf( stdout,
             "pblMapRemoveStr( map, 125 ) data = %s\n",
             data );
	PBL_FREE(data);

    data = (char*)pblMapRemoveStr( map, "not there" );
    fprintf( stdout,
             "pblMapRemoveStr( map, not there ) data = %p\n",
             data );

    rc = pblMapSize( map );
    fprintf( stdout, "pblMapSize( map ) rc = %d\n", rc );

    rc = pblMapIsEmpty( map );
    fprintf( stdout, "pblMapIsEmpty( map ) rc = %d\n", rc );

    pblMapFree( map );
    fprintf( stdout, "pblMapFree( map ) \n" );

    return rc;
}

/*
 * Eclipse CDT does not like more than one main,
 * therefore hide all but one main with this -D option
 */

#ifdef _CDT_BUILD
#define MAP_TST_SHOW_MAIN
#endif

#ifdef PBLTEST
#define MAP_TST_SHOW_MAIN
#endif

#ifdef _WIN32
#define MAP_TST_SHOW_MAIN
#endif

#ifdef CDT_BUILD
#undef MAP_TST_SHOW_MAIN
#endif

#ifdef MAP_TST_SHOW_MAIN

int main( int argc, char * argv[ ] )
{
    return pblMap_TestFrame( argc, argv );
}

#endif /* CDT_BUILD */

