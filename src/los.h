/* Copyright (C) 2000 Patrick Eriksson <patrick@rss.chalmers.se>

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA. */



////////////////////////////////////////////////////////////////////////////
//   File description
////////////////////////////////////////////////////////////////////////////
/**
   \file   los.h

   This file contains the definition of the LOS structure

   \author Patrick Eriksson
   \date 2000-09-18 
*/



#ifndef los_h
#define los_h


////////////////////////////////////////////////////////////////////////////
//   External declarations
////////////////////////////////////////////////////////////////////////////

#include "vecmat.h"



/** The line of sight (LOS). 

    The LOS structure has the fields:
    \verbatim
       ARRAYofVECTOR  p;
       VECTOR         l_step;
       ARRAYofsizet   ground;
       ARRAYofsizet   start;
       ARRAYofsizet   stop;
    where 
       p        The pressures along LOS
       l_step   The geometrical length along LOS between the points.
       start    start index for the iteration
       stop     stop index for the iteration
       ground   -1 if no intersection with the ground. Else, GROUND
                gives the  index for the ground.  
    \endverbatim

    The LOS is defined in equal long geometrical steps along the path.
    This step length (L_STEP) is set to the user defined value, 
    beside for downward observations inside the atmosphere where L_STEP
    is adjusted to the distance between the sensor and the tangent point,
    or the ground. The latter adjustment is done in such way that an integer 
    number of steps is obtained between the two points. The highest
    possible value for L_STEP below the used defined value is selected.

    Spectra are calculated in the following way (by RTE_ITERATE in m_los):
    \begin{enumerate}
    \item Iteration from START down to 0 or GROUND
    \item If GROUND, including the effect of the ground reflection.
    \item Iteration from 0 or GROUND-1 to STOP
    \end{enumerate}

    The START and STOP variables make it possible to use a possible symmetry
    for 1D calculations. For example, for limb sounding from space, START
    and STOP are both set to the length of P. The GROUND variable is for
    1D calculations either -1 or 0.

    For cases without symmetry (upward looking and 2D), STOP is always 1
    and corresponds to the point closest to the sensor. Accordingly, START
    corresponds to the point of LOS furthest away from the sensor.

    The GROUND variable is used both as a flag to indicate ground 
    intersections of the LOS, and a variable to give the position of the
    ground. As mentioned, for 1D cases, the ground is always placed at 
    index 0. For 2D cases, GROUND gives the index for the ground point, 
    that is, the point of LOS with index GROUND corresponds to the ground 
    level.

    \author Patrick Eriksson 
    \date   07.06.00 
*/
struct LOS {
  ARRAYofVECTOR  p;
  VECTOR         l_step;
  ARRAY<int>     ground;
  ARRAYofsizet   start;
  ARRAYofsizet   stop;
};


// A little function to check if there is any ground intersection 
// The function is placed in m_los.cc
//
bool any_ground( const ARRAY<int>& ground );

#endif  // los_h
