/* Copyright (C) 2008 Stefan Buehler <sbuehler(at)ltu.se>

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

/*!
  \file   interpolation_poly.cc
  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-02-04
  
  \brief  Interpolation routines for cubic and higher order interpolation. 
  
  The data structures and functions provided here follow the same
  philosophy as those for linear interpolation in
  interpolation{.h,.cc}. You will need a sequence of three steps to
  perform an interpolation: 

  -# gridpos_poly (one for each interpolation dimension)
  -# interpweights
  -# interp
  
  Not only is the philosophy the same, these higher order functions
  also make direct use of the linear functions in some important
  cases. 
*/

#include <iostream>
#include "interpolation_poly.h"
#include "interpolation.h"
#include "logic.h"
#include "arts_omp.h"

//! Return the maximum of two integer numbers.
/*! 
  This function is based on a macro from Numerical Receipes. The
  original macro:

  static Index imaxarg1, imaxarg2;
  #define IMAX(a,b) (imaxarg1=(a), imaxarg2=(b),(imaxarg1) > (imaxarg2) ? \
                   (imaxarg1) : (imaxarg2))

  The macro can cause trouble if used in parallel regions, so we use this
  function instead.  
                                      
  \param a Input a.
  \param b Input b.

  \return The maximum of a and b.
*/
Index IMAX(Index a, Index b)
{
  return a>b ? a:b;
}

//! Return the minimum of two integer numbers.
/*! 
  This function is based on a macro from Numerical Receipes. The
  original macro:

  static Index iminarg1, iminarg2;
  #define IMIN(a,b) (iminarg1=(a), iminarg2=(b),(iminarg1) < (iminarg2) ? \
                   (iminarg1) : (iminarg2))

  The macro can cause trouble if used in parallel regions, so we use this
  function instead.  
                                      
  \param a Input a.
  \param b Input b.

  \return The minimum of a and b.
*/
Index IMIN(Index a, Index b)
{
  return a<b ? a:b;
}

// File-global constants:

//! The maximum difference from 1 that we allow for a sum check.
/*!
  The sum check makes sure that the sum of all weights is
  approximately 1.

  We cannot use a sharp comparison there, due to numerical
  noise. The value of 1e-6 is an ad-hoc value.

  This shold be ok, the main point of the test is to make sure that
  what we have really *are* interpolation weights, and not something
  else. 
*/
const Numeric sum_check_epsilon = 1e-6;

//! Set up grid positions for higher order interpolation.
/*!
  This function performs the same task as gridpos, but for arbitrary
  orders of interpolation. (Linear is also possible, as a special
  case.) 
  
  The formula for calculating the weights w is taken from Numerical
  Recipes, 2nd edition, section 3.1, eq. 3.1.1.

  \param gp Output: An array of grid positions.
  \param old_grid Original grid.
  \param new_grid New grid.
  \param order Interpolation order.
               1 = linear, 2 = quadratic, etc..
               The number of points used in the
               interpolation will be order+1.
  \param extpolfac Extrapolation fraction. Should normally not be
                   specified, then the default of 0.5 is used. 
*/
void gridpos_poly(ArrayOfGridPosPoly& gp,
                  ConstVectorView old_grid,
                  ConstVectorView new_grid,
                  const Index     order,
                  const Numeric&  extpolfac)
{
  // Number of points used in the interpolation (order + 1):
  Index m=order+1;

  const Index n_old = old_grid.nelem();
  const Index n_new = new_grid.nelem();

  // Since we need m interpolation points, the old grid must have a
  // least m elements. 
  assert(n_old>=m);

  // Consistently with gridpos, the array size of gp has to be set
  // outside. Here, we only assert that it is correct:
  assert( is_size(gp,n_new) );
  
  // First call the traditional gridpos to find the grid positions:
  ArrayOfGridPos gp_trad(n_new);
  gridpos( gp_trad, old_grid, new_grid, extpolfac );

  for (Index s=0; s<n_new; ++s)
    {
                   
      // Here we calculate the index of the first of the range of
      // points used for interpolation. For linear interpolation this
      // is identical to j. The idea for this expression is from
      // Numerical Receipes (Chapter 3, section "after the hunt"), but
      // there is is for 1-based arrays.
      Index k = IMIN(IMAX(gp_trad[s].idx-(m-1)/2, 0),
                     n_old-m);

      //      cout << "m: "<< m << ", k: " << k << endl;


      // Make gp[s].idx and gp[s].w the right size:
      gp[s].idx.resize(m);
      gp[s].w.resize(m);
      
      // Calculate w for each interpolation point. In the linear case
      // these are just the fractional distances to each interpolation
      // point. The w here correspond exactly to the terms in front of
      // the yi in Numerical Recipes, 2nd edition, section 3.1,
      // eq. 3.1.1.
      for (Index i=0; i<m; ++i)
        {
          gp[s].idx[i] = k+i;

          //  Numerical Recipes, 2nd edition, section 3.1, eq. 3.1.1.

          // Numerator:
          Numeric num = 1;
          for (Index j=0; j<m; ++j)
            if (j!=i)
              num *= new_grid[s] - old_grid[k+j];
      
          // Denominator:
          Numeric denom = 1;
          for (Index j=0; j<m; ++j)
            if (j!=i)
              denom *= old_grid[k+i] - old_grid[k+j];

          gp[s].w[i] = num / denom;
        }

      // Debugging: Test if sum of all w is 1, as it should be:
//       Numeric testsum = 0;
//       for (Index i=0; i<m; ++i) testsum += gp[s].w[i];
//       cout << "Testsum = " << testsum << endl;        
      
    }
}


//! gridpos_poly
/*!
   Creates a grid position structure for higher order interpolation.
  
   This is a gateway to the function for arrays of GridPosPoly, to be
   used for e.g. "red interpolation".

   \retval  gp         The GridPos structure. 
   \param   old_grid   The original grid.
   \param   new_grid   The position where we want to have the interpolated 
                       value.
   \param order Interpolation order.
                1 = linear, 2 = quadratic, etc..
                The number of points used in the
                interpolation will be order+1.
   \param   extpolfac  Extrapolation factor. Default value is 0.5,
                       which means that extrapolation of half of the
                       last grid distance is allowed.
                       You don't have to specify this.

   \author Stefan Buehler
   \date   2008-03-06
*/
void gridpos_poly( GridPosPoly&    gp,
                   ConstVectorView old_grid,
                   const Numeric&  new_grid,
                   const Index     order,
                   const Numeric&  extpolfac )
{
  ArrayOfGridPosPoly  agp(1);
  gridpos_poly( agp, old_grid, new_grid, order, extpolfac );
  gp = agp[0];  
}






// FIXME: Below here is copied code from interpolation.cc that must be adapted.

//! Macro for interpolation weight loops.
/*!
  We use the macro LOOPW to make the notation for the nested for
  loops in the interpweights functions more concise, and to avoid
  typing errors.

  Should resolve to something like:

  for ( Index x=0; x<tx.w.nelem(); ++x )

  But with iterators:

  for ( ConstIterator1D x=tx.w.begin(); x!=tx.w.end(); ++x )

*/
//#define LOOPW(x) for ( const Numeric* x=&t##x.fd[1]; x>=&t##x.fd[0]; --x )
//#define LOOPW(x) for ( Index x=0; x<t##x.w.nelem(); ++x )
#define LOOPW(x) for ( ConstIterator1D x=t##x.w.begin(); x!=t##x.w.end(); ++x )

//! Macro for interpolation index loops.
/*!
  This is the same as LOOPW, but for loops over tx.idx. Since tx.idx
  is an ArrayOfIndex, not a Vector, we have to use a different type of
  iterator.
*/
#define LOOPIDX(x) for (ArrayOfIndex::const_iterator x=t##x.idx.begin(); x!=t##x.idx.end(); ++x)


//! Output operator for GridPosPoly.
/*!
  This is just intended for testing and debugging.
  
  \param os Output stream.
  \param gp Grid position.

  \return The output stream.
*/
ostream& operator<<(ostream& os, const GridPosPoly& gp)
{
  os << "idx: " << gp.idx << "\n";
  os << "w:   " << gp.w   << "\n";

//   cout << "Test iterator:\n";
//   for (ArrayOfIndex::const_iterator x=gp.idx.begin(); x!=gp.idx.end(); ++x)
//     cout << *x << ":";
//   cout << "\n";

  return os;
}






////////////////////////////////////////////////////////////////////////////
//                      Red Interpolation
////////////////////////////////////////////////////////////////////////////

//! Red 1D interpolation weights.
/*! 
  "Red" interpolation returns just a scalar, so the weights are stored
  in a Vector.   

  The length of itw must be consistent with the dimension of the
  field to be interpolated (2^n).

  \retval itw Interpolation weights.
  \param  tc  The grid position for the column dimension.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
void interpweights( VectorView itw,
                    const GridPosPoly& tc )
{
  assert(is_size(itw,tc.w.nelem()));       

  // Interpolation weights are stored in this order (l=lower
  // u=upper, c=column):
  // 1. l-c
  // 2. u-c

  Index iti = 0;

  LOOPW(c)
    {
      itw[iti] = *c;
      ++iti;
    }
}

//! Red 2D interpolation weights.
/*! 
  "Red" interpolation returns just a scalar, so the weights are stored
  in a Vector.   

  The length of itw must be consistent with the dimension of the
  field to be interpolated (2^n).

  \retval itw Interpolation weights.
  \param tr   The grid position for the row dimension.
  \param tc   The grid position for the column dimension.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
void interpweights( VectorView itw,
                    const GridPosPoly& tr,
                    const GridPosPoly& tc )
{
  assert(is_size(itw,
                 tr.w.nelem()*
                 tc.w.nelem())); 
  Index iti = 0;

  LOOPW(r)
  LOOPW(c)
    {
      itw[iti] = (*r) * (*c);
      ++iti;
    }
}

//! Red 3D interpolation weights.
/*! 
  "Red" interpolation returns just a scalar, so the weights are stored
  in a Vector.   

  The length of itw must be consistent with the dimension of the
  field to be interpolated (2^n).

  \retval itw Interpolation weights.
  \param tp   The grid position for the page    dimension.
  \param tr   The grid position for the row     dimension.
  \param tc   The grid position for the column  dimension.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
void interpweights( VectorView itw,
                    const GridPosPoly& tp,
                    const GridPosPoly& tr,
                    const GridPosPoly& tc )
{
  assert(is_size(itw,
                 tp.w.nelem()*
                 tr.w.nelem()*
                 tc.w.nelem()));       
                               
  Index iti = 0;

  LOOPW(p)
  LOOPW(r)
  LOOPW(c)
    {
      itw[iti] = (*p) * (*r) * (*c);
      ++iti;
    }
}

//! Red 4D interpolation weights.
/*! 
  "Red" interpolation returns just a scalar, so the weights are stored
  in a Vector.   

  The length of itw must be consistent with the dimension of the
  field to be interpolated (2^n).

  \retval itw Interpolation weights.
  \param tb   The grid position for the book    dimension.
  \param tp   The grid position for the page    dimension.
  \param tr   The grid position for the row     dimension.
  \param tc   The grid position for the column  dimension.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
void interpweights( VectorView itw,
                    const GridPosPoly& tb,
                    const GridPosPoly& tp,
                    const GridPosPoly& tr,
                    const GridPosPoly& tc )
{
  assert(is_size(itw,
                 tb.w.nelem()*
                 tp.w.nelem()*
                 tr.w.nelem()*
                 tc.w.nelem()));      
                                
  Index iti = 0;

  LOOPW(b)
  LOOPW(p)
  LOOPW(r)
  LOOPW(c)
    {
      itw[iti] = (*b) * (*p) * (*r) * (*c);
      ++iti;
    }
}

//! Red 5D interpolation weights.
/*! 
  "Red" interpolation returns just a scalar, so the weights are stored
  in a Vector.   

  The length of itw must be consistent with the dimension of the
  field to be interpolated (2^n).

  \retval itw Interpolation weights.
  \param ts   The grid position for the shelf   dimension.
  \param tb   The grid position for the book    dimension.
  \param tp   The grid position for the page    dimension.
  \param tr   The grid position for the row     dimension.
  \param tc   The grid position for the column  dimension.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
void interpweights( VectorView itw,
                    const GridPosPoly& ts,
                    const GridPosPoly& tb,
                    const GridPosPoly& tp,
                    const GridPosPoly& tr,
                    const GridPosPoly& tc )
{
  assert(is_size(itw,
                 ts.w.nelem()*
                 tb.w.nelem()*
                 tp.w.nelem()*
                 tr.w.nelem()*
                 tc.w.nelem()));      
                                
  Index iti = 0;

  LOOPW(s)
  LOOPW(b)
  LOOPW(p)
  LOOPW(r)
  LOOPW(c)
    {
      itw[iti] = (*s) * (*b) * (*p) * (*r) * (*c);
      ++iti;
    }
}

//! Red 6D interpolation weights.
/*! 
  "Red" interpolation returns just a scalar, so the weights are stored
  in a Vector.   

  The length of itw must be consistent with the dimension of the
  field to be interpolated (2^n).

  \retval itw Interpolation weights.
  \param  tv  The grid position for the vitrine dimension.
  \param  ts  The grid position for the shelf   dimension.
  \param  tb  The grid position for the book    dimension.
  \param  tp  The grid position for the page    dimension.
  \param  tr  The grid position for the row     dimension.
  \param  tc  The grid position for the column  dimension.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
void interpweights( VectorView itw,
                    const GridPosPoly& tv,
                    const GridPosPoly& ts,
                    const GridPosPoly& tb,
                    const GridPosPoly& tp,
                    const GridPosPoly& tr,
                    const GridPosPoly& tc )
{
  assert(is_size(itw,
                 tv.w.nelem()*
                 ts.w.nelem()*
                 tb.w.nelem()*
                 tp.w.nelem()*
                 tr.w.nelem()*
                 tc.w.nelem()));      
                                
  Index iti = 0;

  LOOPW(v)
  LOOPW(s)
  LOOPW(b)
  LOOPW(p)
  LOOPW(r)
  LOOPW(c)
    {
      itw[iti] = (*v) * (*s) * (*b) * (*p) * (*r) * (*c);
      ++iti;
    }
}

//! Red 1D Interpolate.
/*! 
  "Red" interpolation returns just a scalar.

  The dimension of itw must be consistent with the dimension of the
  interpolation (2^n).

  \param itw  Interpolation weights.
  \param a    The field to interpolate.
  \param tc   The grid position for the column dimension.

  \return Interpolated value.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
Numeric interp( ConstVectorView itw,
                ConstVectorView a,    
                const GridPosPoly&  tc )
{
  assert(is_size(itw,tc.w.nelem()));       

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one.
  assert( is_same_within_epsilon( itw.sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // To store interpolated value:
  Numeric tia = 0;

  Index iti = 0;
  LOOPIDX(c)
    {
      tia += a[*c] * itw[iti];
      ++iti;
    }

  return tia;
}

//! Red 2D Interpolate.
/*! 
  "Red" interpolation returns just a scalar.

  The dimension of itw must be consistent with the dimension of the
  interpolation (2^n).

  \param itw  Interpolation weights.
  \param a    The field to interpolate.
  \param tr   The grid position for the row     dimension.
  \param tc   The grid position for the column  dimension.

  \return Interpolated value.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
Numeric interp( ConstVectorView  itw,
                ConstMatrixView  a,    
                const GridPosPoly&   tr,
                const GridPosPoly&   tc )
{
  assert(is_size(itw,
                 tr.w.nelem()*
                 tc.w.nelem()));       

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one.
  assert( is_same_within_epsilon( itw.sum(),
                                  1,
                                  sum_check_epsilon ) );

  // To store interpolated value:
  Numeric tia = 0;

  Index iti = 0;
  LOOPIDX(r)
    LOOPIDX(c)
      {
        tia += a(*r,
                 *c) * itw[iti];
        ++iti;
      }

  return tia;
}

//! Red 3D Interpolate.
/*! 
  "Red" interpolation returns just a scalar.

  The dimension of itw must be consistent with the dimension of the
  interpolation (2^n).

  \param itw  Interpolation weights.
  \param a    The field to interpolate.
  \param tp   The grid position for the page    dimension.
  \param tr   The grid position for the row     dimension.
  \param tc   The grid position for the column  dimension.

  \return Interpolated value.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
Numeric interp( ConstVectorView  itw,
                ConstTensor3View a,    
                const GridPosPoly&   tp,
                const GridPosPoly&   tr,
                const GridPosPoly&   tc )
{
  assert(is_size(itw,
                 tp.w.nelem()*
                 tr.w.nelem()*
                 tc.w.nelem()));      

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one.
  assert( is_same_within_epsilon( itw.sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // To store interpolated value:
  Numeric tia = 0;

  Index iti = 0;
  LOOPIDX(p)
  LOOPIDX(r)
  LOOPIDX(c)
        {
          tia += a(*p,
                   *r,
                   *c) * itw[iti];
          ++iti;
        }

  return tia;
}

//! Red 4D Interpolate.
/*! 
  "Red" interpolation returns just a scalar.

  The dimension of itw must be consistent with the dimension of the
  interpolation (2^n).

  \param itw  Interpolation weights.
  \param a    The field to interpolate.
  \param tb   The grid position for the book    dimension.
  \param tp   The grid position for the page    dimension.
  \param tr   The grid position for the row     dimension.
  \param tc   The grid position for the column  dimension.

  \return Interpolated value.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
Numeric interp( ConstVectorView  itw,
                ConstTensor4View a,    
                const GridPosPoly&   tb,
                const GridPosPoly&   tp,
                const GridPosPoly&   tr,
                const GridPosPoly&   tc )
{
  assert(is_size(itw,
                 tb.w.nelem()*
                 tp.w.nelem()*
                 tr.w.nelem()*
                 tc.w.nelem()));      

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one.
  assert( is_same_within_epsilon( itw.sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // To store interpolated value:
  Numeric tia = 0;

  Index iti = 0;
  LOOPIDX(b)
  LOOPIDX(p)
  LOOPIDX(r)
  LOOPIDX(c)
          {
            tia += a(*b,
                     *p,
                     *r,
                     *c) * itw[iti];
            ++iti;
          }

  return tia;
}

//! Red 5D Interpolate.
/*! 
  "Red" interpolation returns just a scalar.

  The dimension of itw must be consistent with the dimension of the
  interpolation (2^n).

  \param itw  Interpolation weights.
  \param a    The field to interpolate.
  \param ts   The grid position for the shelf   dimension.
  \param tb   The grid position for the book    dimension.
  \param tp   The grid position for the page    dimension.
  \param tr   The grid position for the row     dimension.
  \param tc   The grid position for the column  dimension.

  \return Interpolated value.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
Numeric interp( ConstVectorView  itw,
                ConstTensor5View a,    
                const GridPosPoly&   ts,
                const GridPosPoly&   tb,
                const GridPosPoly&   tp,
                const GridPosPoly&   tr,
                const GridPosPoly&   tc )
{
  assert(is_size(itw,
                 ts.w.nelem()*
                 tb.w.nelem()*
                 tp.w.nelem()*
                 tr.w.nelem()*
                 tc.w.nelem()));      

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one.
  assert( is_same_within_epsilon( itw.sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // To store interpolated value:
  Numeric tia = 0;

  Index iti = 0;
  LOOPIDX(s)
  LOOPIDX(b)
  LOOPIDX(p)
  LOOPIDX(r)
  LOOPIDX(c)
            {
              tia += a(*s,
                       *b,
                       *p,
                       *r,
                       *c) * itw[iti];
              ++iti;
            }

  return tia;
}

//! Red 6D Interpolate.
/*! 
  "Red" interpolation returns just a scalar.

  The dimension of itw must be consistent with the dimension of the
  interpolation (2^n).

  \param itw  Interpolation weights.
  \param a    The field to interpolate.
  \param tv   The grid position for the vitrine dimension.
  \param ts   The grid position for the shelf   dimension.
  \param tb   The grid position for the book    dimension.
  \param tp   The grid position for the page    dimension.
  \param tr   The grid position for the row     dimension.
  \param tc   The grid position for the column  dimension.

  \return Interpolated value.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
Numeric interp( ConstVectorView  itw,
                ConstTensor6View a,    
                const GridPosPoly&   tv,
                const GridPosPoly&   ts,
                const GridPosPoly&   tb,
                const GridPosPoly&   tp,
                const GridPosPoly&   tr,
                const GridPosPoly&   tc )
{
  assert(is_size(itw,
                 tv.w.nelem()*
                 ts.w.nelem()*
                 tb.w.nelem()*
                 tp.w.nelem()*
                 tr.w.nelem()*
                 tc.w.nelem()));      

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one.
  assert( is_same_within_epsilon( itw.sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // To store interpolated value:
  Numeric tia = 0;

  Index iti = 0;
  LOOPIDX(v)
  LOOPIDX(s)
  LOOPIDX(b)
  LOOPIDX(p)
  LOOPIDX(r)
  LOOPIDX(c)
              {
                tia += a(*v,
                         *s,
                         *b,
                         *p,
                         *r,
                         *c) * itw[iti];
                ++iti;
              }

  return tia;
}



////////////////////////////////////////////////////////////////////////////
//                      Blue interpolation
////////////////////////////////////////////////////////////////////////////

//! Compute 1D interpolation weights.
/*! 
  For this 1D case there is no distinction between "blue" and "green"
  type interpolation.

  The dimensions of itw must be consistent with cgp.

  Note that we still do not need the actual field for this step.

  
  \retval itw Interpolation weights.
  \param  cgp The grid position Array for the column dimension.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
void interpweights( MatrixView itw,
                    const ArrayOfGridPosPoly& cgp )
{
  Index n = cgp.nelem();
  assert(is_size(itw,n,
                 cgp[0].w.nelem()));     
                                

  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tc = cgp[i];

      // Interpolation weights are stored in this order (l=lower
      // u=upper, c=column):
      // 1. l-c
      // 2. u-c

      Index iti = 0;

      // We could use a straight-forward for loop here:
      //
      //       for ( Index c=1; c>=0; --c )
      //        {
      //          ti[iti] = tc.fd[c];
      //          ++iti;
      //        }
      //
      // However, it is slightly faster to use pointers (I tried it,
      // the speed gain is about 20%). So we should write something
      // like:
      //
      //       for ( const Numeric* c=&tc.fd[1]; c>=&tc.fd[0]; --c )
      //        {
      //          ti[iti] = *c;
      //          ++iti;
      //        }
      //
      // For higher dimensions we have to nest these loops. To avoid
      // typos and safe typing, I use the LOOPW macro, which expands
      // to the for loop above. Note: NO SEMICOLON AFTER THE LOOPW
      // COMMAND! 

      LOOPW(c)
        {
          itw(i,iti) = *c;
          ++iti;
        }
    }
}

//! Compute 2D interpolation weights for a sequence of positions.
/*! 
 Compute the weights for a "blue" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 a sequence of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The dimensions of itw must be also consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Matrix, whereas in the other case it is a Tensor with one
 more dimension than there are input grid position Arrays.

 \retval itw Interpolation weights.
 \param  rgp The grid position Array for the row dimension.
 \param  cgp The grid position Array for the column dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( MatrixView itw,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index n = cgp.nelem();
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));     
                                

  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      // Interpolation weights are stored in this order (l=lower
      // u=upper, r=row, c=column):
      // 1. l-r l-c
      // 2. l-r u-c
      // 3. u-r l-c
      // 4. u-r u-c

      Index iti = 0;

      LOOPW(r)
      LOOPW(c)
          {
            itw(i,iti) = (*r) * (*c);
            ++iti;
          }
    }
}

//! Compute 3D interpolation weights for a sequence of positions.
/*! 
 Compute the weights for a "blue" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 a sequence of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The dimensions of itw must be also consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Matrix, whereas in the other case it is a Tensor with one
 more dimension than there are input grid position Arrays.

 \retval itw Interpolation weights.
 \param  pgp The grid position Array for the page dimension.
 \param  rgp The grid position Array for the row dimension.
 \param  cgp The grid position Array for the column dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( MatrixView itw,
                    const ArrayOfGridPosPoly& pgp,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index n = cgp.nelem();
  assert(is_size(pgp,n));       // pgp must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));     
                                

  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tp = pgp[i];
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      Index iti = 0;
      LOOPW(p)
      LOOPW(r)
      LOOPW(c)
        {
          itw(i,iti) = (*p) * (*r) * (*c);
          ++iti;
        }
    }
}

//! Compute 4D interpolation weights for a sequence of positions.
/*! 
 Compute the weights for a "blue" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 a sequence of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The dimensions of itw must be also consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Matrix, whereas in the other case it is a Tensor with one
 more dimension than there are input grid position Arrays.

 \retval itw Interpolation weights.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( MatrixView itw,
                    const ArrayOfGridPosPoly& bgp,
                    const ArrayOfGridPosPoly& pgp,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index n = cgp.nelem();
  assert(is_size(bgp,n));       // bgp must have same size as cgp.
  assert(is_size(pgp,n));       // pgp must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));    
                                

  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tb = bgp[i];
      const GridPosPoly& tp = pgp[i];
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      Index iti = 0;
      LOOPW(b)
      LOOPW(p)
      LOOPW(r)
      LOOPW(c)
        {
          itw(i,iti) = (*b) * (*p) * (*r) * (*c);
          ++iti;
        }
    }
}

//! Compute 5D interpolation weights for a sequence of positions.
/*! 
 Compute the weights for a "blue" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 a sequence of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The dimensions of itw must be also consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Matrix, whereas in the other case it is a Tensor with one
 more dimension than there are input grid position Arrays.

 \retval itw Interpolation weights.
 \param sgp The grid position Array for the shelf   dimension.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( MatrixView itw,
                    const ArrayOfGridPosPoly& sgp,
                    const ArrayOfGridPosPoly& bgp,
                    const ArrayOfGridPosPoly& pgp,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index n = cgp.nelem();
  assert(is_size(sgp,n));       // sgp must have same size as cgp.
  assert(is_size(bgp,n));       // bgp must have same size as cgp.
  assert(is_size(pgp,n));       // pgp must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 sgp[0].w.nelem()*
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));    
                                

  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& ts = sgp[i];
      const GridPosPoly& tb = bgp[i];
      const GridPosPoly& tp = pgp[i];
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      Index iti = 0;
      LOOPW(s)
      LOOPW(b)
      LOOPW(p)
      LOOPW(r)
      LOOPW(c)
        {
          itw(i,iti) = (*s) * (*b) * (*p) * (*r) * (*c);
          ++iti;
        }
    }
}

//! Compute 6D interpolation weights for a sequence of positions.
/*! 
 Compute the weights for a "blue" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 a sequence of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The dimensions of itw must be also consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Matrix, whereas in the other case it is a Tensor with one
 more dimension than there are input grid position Arrays.

 \retval itw Interpolation weights.
 \param vgp The grid position Array for the vitrine dimension.
 \param sgp The grid position Array for the shelf   dimension.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( MatrixView itw,
                    const ArrayOfGridPosPoly& vgp,
                    const ArrayOfGridPosPoly& sgp,
                    const ArrayOfGridPosPoly& bgp,
                    const ArrayOfGridPosPoly& pgp,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index n = cgp.nelem();
  assert(is_size(vgp,n));       // vgp must have same size as cgp.
  assert(is_size(sgp,n));       // sgp must have same size as cgp.
  assert(is_size(bgp,n));       // bgp must have same size as cgp.
  assert(is_size(pgp,n));       // pgp must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 vgp[0].w.nelem()*
                 sgp[0].w.nelem()*
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));    
                                

  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tv = vgp[i];
      const GridPosPoly& ts = sgp[i];
      const GridPosPoly& tb = bgp[i];
      const GridPosPoly& tp = pgp[i];
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      Index iti = 0;
      LOOPW(v)
      LOOPW(s)
      LOOPW(b)
      LOOPW(p)
      LOOPW(r)
      LOOPW(c)
        {
          itw(i,iti) = (*v) * (*s) * (*b) * (*p) * (*r) * (*c);
          ++iti;
        }
    }
}

//! Interpolate 1D field.
/*! 
  For this 1D case there is no distinction between "blue" and "green"
  type interpolation.

  The output vector ia must have the same length as the grid position
  vector cgp. And the dimension of itw must be consistent with
  this.

  \retval ia  Vector containing the interpolated field values.
  \param itw Interpolation weights.
  \param a   The field to interpolate.
  \param cgp The grid position Array for the column dimension.

  \author Stefan Buehler <sbuehler(at)ltu.se>
  \date   2008-03-06
*/
void interp( VectorView            ia,
             ConstMatrixView       itw,
             ConstVectorView       a,    
             const ArrayOfGridPosPoly& cgp)
{
  Index n = cgp.nelem();
  assert(is_size(ia,n));        //  ia must have same size as cgp.
  assert(is_size(itw,n,
                 cgp[0].w.nelem()));     

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tc = cgp[i];

      // Get handle to current element of output vector and initialize
      // it to zero:
      Numeric& tia = ia[i];
      tia = 0;

      Index iti = 0;
      LOOPIDX(c)
        {
          tia += a[*c] * itw(i,iti);
          ++iti;
        }
    }
}

//! Interpolate 2D field to a sequence of positions.
/*! 
 This performs a "blue" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining a sequence
 of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The output vector ia also must have the same length. And the
 dimension of itw must be consistent with this.

 This function can be easily distinguished from the other
 interpolation function (that creates an entire field of interpolated
 values), because of the dimension of ia and itw.

 \retval ia  Vector containing the interpolated field values.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param rgp The grid position Array for the row    dimension.
 \param cgp The grid position Array for the column dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( VectorView            ia,
             ConstMatrixView       itw,
             ConstMatrixView       a,    
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index n = cgp.nelem();
  assert(is_size(ia,n));        //  ia must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));     
  
  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      // Get handle to current element of output vector and initialize
      // it to zero:
      Numeric& tia = ia[i];
      tia = 0;

      Index iti = 0;
      LOOPIDX(r)
        LOOPIDX(c)
          {
            tia += a(*r,
                     *c) * itw(i,iti);
            ++iti;
          }
    }
}

//! Interpolate 3D field to a sequence of positions.
/*! 
 This performs a "blue" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining a sequence
 of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The output vector ia also must have the same length. And the
 dimension of itw must be consistent with this.

 This function can be easily distinguished from the other
 interpolation function (that creates an entire field of interpolated
 values), because of the dimension of ia and itw.

 \retval ia  Vector containing the interpolated field values.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( VectorView            ia,
             ConstMatrixView       itw,
             ConstTensor3View      a,    
             const ArrayOfGridPosPoly& pgp,
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index n = cgp.nelem();
  assert(is_size(ia,n));        //  ia must have same size as cgp.
  assert(is_size(pgp,n));       // pgp must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));     
  
  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tp = pgp[i];
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      // Get handle to current element of output vector and initialize
      // it to zero:
      Numeric& tia = ia[i];
      tia = 0;

      Index iti = 0;
      LOOPIDX(p)
      LOOPIDX(r)
      LOOPIDX(c)
            {
              tia += a(*p,
                       *r,
                       *c) * itw(i,iti);
              ++iti;
            }
    }
}

//! Interpolate 4D field to a sequence of positions.
/*! 
 This performs a "blue" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining a sequence
 of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The output vector ia also must have the same length. And the
 dimension of itw must be consistent with this.

 This function can be easily distinguished from the other
 interpolation function (that creates an entire field of interpolated
 values), because of the dimension of ia and itw.

 \retval ia  Vector containing the interpolated field values.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( VectorView            ia,
             ConstMatrixView       itw,
             ConstTensor4View      a,    
             const ArrayOfGridPosPoly& bgp,
             const ArrayOfGridPosPoly& pgp,
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index n = cgp.nelem();
  assert(is_size(ia,n));        //  ia must have same size as cgp.
  assert(is_size(bgp,n));       // bgp must have same size as cgp.
  assert(is_size(pgp,n));       // pgp must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));    
  
  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tb = bgp[i];
      const GridPosPoly& tp = pgp[i];
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      // Get handle to current element of output vector and initialize
      // it to zero:
      Numeric& tia = ia[i];
      tia = 0;

      Index iti = 0;
      LOOPIDX(b)
      LOOPIDX(p)
      LOOPIDX(r)
      LOOPIDX(c)
              {
                tia += a(*b,
                         *p,
                         *r,
                         *c) * itw(i,iti);
                ++iti;
              }
    }
}

//! Interpolate 5D field to a sequence of positions.
/*! 
 This performs a "blue" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining a sequence
 of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The output vector ia also must have the same length. And the
 dimension of itw must be consistent with this.

 This function can be easily distinguished from the other
 interpolation function (that creates an entire field of interpolated
 values), because of the dimension of ia and itw.

 \retval ia  Vector containing the interpolated field values.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param sgp The grid position Array for the shelf   dimension.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( VectorView            ia,
             ConstMatrixView       itw,
             ConstTensor5View      a,    
             const ArrayOfGridPosPoly& sgp,
             const ArrayOfGridPosPoly& bgp,
             const ArrayOfGridPosPoly& pgp,
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index n = cgp.nelem();
  assert(is_size(ia,n));        //  ia must have same size as cgp.
  assert(is_size(sgp,n));       // sgp must have same size as cgp.
  assert(is_size(bgp,n));       // bgp must have same size as cgp.
  assert(is_size(pgp,n));       // pgp must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 sgp[0].w.nelem()*
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));    
  
  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& ts = sgp[i];
      const GridPosPoly& tb = bgp[i];
      const GridPosPoly& tp = pgp[i];
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      // Get handle to current element of output vector and initialize
      // it to zero:
      Numeric& tia = ia[i];
      tia = 0;

      Index iti = 0;
      LOOPIDX(s)
      LOOPIDX(b)
      LOOPIDX(p)
      LOOPIDX(r)
      LOOPIDX(c)
                {
                  tia += a(*s,
                           *b,
                           *p,
                           *r,
                           *c) * itw(i,iti);
                  ++iti;
                }
    }
}

//! Interpolate 6D field to a sequence of positions.
/*! 
 This performs a "blue" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining a sequence
 of positions. ALL GRID POSITION ARRAYS MUST HAVE THE SAME LENGTH! 

 The output vector ia also must have the same length. And the
 dimension of itw must be consistent with this.

 This function can be easily distinguished from the other
 interpolation function (that creates an entire field of interpolated
 values), because of the dimension of ia and itw.

 \retval ia  Vector containing the interpolated field values.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param vgp The grid position Array for the vitrine dimension.
 \param sgp The grid position Array for the shelf   dimension.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( VectorView            ia,
             ConstMatrixView       itw,
             ConstTensor6View      a,    
             const ArrayOfGridPosPoly& vgp,
             const ArrayOfGridPosPoly& sgp,
             const ArrayOfGridPosPoly& bgp,
             const ArrayOfGridPosPoly& pgp,
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index n = cgp.nelem();
  assert(is_size(ia,n));        //  ia must have same size as cgp.
  assert(is_size(vgp,n));       // vgp must have same size as cgp.
  assert(is_size(sgp,n));       // sgp must have same size as cgp.
  assert(is_size(bgp,n));       // bgp must have same size as cgp.
  assert(is_size(pgp,n));       // pgp must have same size as cgp.
  assert(is_size(rgp,n));       // rgp must have same size as cgp.
  assert(is_size(itw,n,
                 vgp[0].w.nelem()*
                 sgp[0].w.nelem()*
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));    
  
  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the sequence:
  for ( Index i=0; i<n; ++i )
    {
      // Current grid positions:
      const GridPosPoly& tv = vgp[i];
      const GridPosPoly& ts = sgp[i];
      const GridPosPoly& tb = bgp[i];
      const GridPosPoly& tp = pgp[i];
      const GridPosPoly& tr = rgp[i];
      const GridPosPoly& tc = cgp[i];

      // Get handle to current element of output vector and initialize
      // it to zero:
      Numeric& tia = ia[i];
      tia = 0;

      Index iti = 0;
      LOOPIDX(v)
      LOOPIDX(s)
      LOOPIDX(b)
      LOOPIDX(p)
      LOOPIDX(r)
      LOOPIDX(c)
                  {
                    tia += a(*v,
                             *s,
                             *b,
                             *p,
                             *r,
                             *c) * itw(i,iti);
                    ++iti;
                  }
    }
}

////////////////////////////////////////////////////////////////////////////
//                      Green interpolation
////////////////////////////////////////////////////////////////////////////

//! Compute 2D interpolation weights for an entire field.
/*! 
 Compute the weights for a "green" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 the grids for the interpolated field.

 The dimensions of itw must be consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Tensor with one more dimension than the number of grid
 position Arrays.

 \retval itw Interpolation weights
 \param rgp The grid position Array for the row dimension.
 \param cgp The grid position Array for the column dimension.
 
 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( Tensor3View itw,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();
  assert(is_size(itw,nr,nc,
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem())); 
                                

  // We have to loop all the points in the new grid:
  for ( Index ir=0; ir<nr; ++ir )
    {
      // Current grid position:
      const GridPosPoly& tr = rgp[ir];

      for ( Index ic=0; ic<nc; ++ic )
        {
          // Current grid position:
          const GridPosPoly& tc = cgp[ic];

          // Interpolation weights are stored in this order (l=lower
          // u=upper, r=row, c=column):
          // 1. l-r l-c
          // 2. l-r u-c
          // 3. u-r l-c
          // 4. u-r u-c

          Index iti = 0;

          LOOPW(r)
            LOOPW(c)
            {
              itw(ir,ic,iti) = (*r) * (*c);
              ++iti;
            }
        }
    }
}

//! Compute 3D interpolation weights for an entire field.
/*! 
 Compute the weights for a "green" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 the grids for the interpolated field.

 The dimensions of itw must be consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Tensor with one more dimension than the number of grid
 position Arrays.

 \retval itw Interpolation weights
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( Tensor4View itw,
                    const ArrayOfGridPosPoly& pgp,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index np = pgp.nelem();
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();

  assert(is_size(itw,np,nr,nc,
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));      

  // We have to loop all the points in the new grid:
  for ( Index ip=0; ip<np; ++ip )
    {
      const GridPosPoly& tp = pgp[ip];
      for ( Index ir=0; ir<nr; ++ir )
        {
          const GridPosPoly& tr = rgp[ir];
          for ( Index ic=0; ic<nc; ++ic )
            {
              const GridPosPoly& tc = cgp[ic];

              Index iti = 0;

              LOOPW(p)
                LOOPW(r)
                LOOPW(c)
                {
                  itw(ip,ir,ic,iti) =
                    (*p) * (*r) * (*c);
                  ++iti;
                }
            }
        }
    }
}

//! Compute 4D interpolation weights for an entire field.
/*! 
 Compute the weights for a "green" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 the grids for the interpolated field.

 The dimensions of itw must be consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Tensor with one more dimension than the number of grid
 position Arrays.

 \retval itw Interpolation weights
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.
 
 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( Tensor5View itw,
                    const ArrayOfGridPosPoly& bgp,
                    const ArrayOfGridPosPoly& pgp,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index nb = bgp.nelem();
  Index np = pgp.nelem();
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();

  assert(is_size(itw,nb,np,nr,nc,
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));  

  // We have to loop all the points in the new grid:
  for ( Index ib=0; ib<nb; ++ib )
    {
      const GridPosPoly& tb = bgp[ib];
      for ( Index ip=0; ip<np; ++ip )
        {
          const GridPosPoly& tp = pgp[ip];
          for ( Index ir=0; ir<nr; ++ir )
            {
              const GridPosPoly& tr = rgp[ir];
              for ( Index ic=0; ic<nc; ++ic )
                {
                  const GridPosPoly& tc = cgp[ic];

                  Index iti = 0;

                  LOOPW(b)
                    LOOPW(p)
                    LOOPW(r)
                    LOOPW(c)
                    {
                      itw(ib,ip,ir,ic,iti) =
                        (*b) * (*p) * (*r) * (*c);
                      ++iti;
                    }
                }
            }
        }
    }
}

//! Compute 5D interpolation weights for an entire field.
/*! 
 Compute the weights for a "green" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 the grids for the interpolated field.

 The dimensions of itw must be consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Tensor with one more dimension than the number of grid
 position Arrays.

 \retval itw Interpolation weights
 \param sgp The grid position Array for the shelf   dimension.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.
 
 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( Tensor6View itw,
                    const ArrayOfGridPosPoly& sgp,
                    const ArrayOfGridPosPoly& bgp,
                    const ArrayOfGridPosPoly& pgp,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index ns = sgp.nelem();
  Index nb = bgp.nelem();
  Index np = pgp.nelem();
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();

  assert(is_size(itw,ns,nb,np,nr,nc,
                 sgp[0].w.nelem()*
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));       

  // We have to loop all the points in the new grid:
  for ( Index is=0; is<ns; ++is )
    {
      const GridPosPoly& ts = sgp[is];
      for ( Index ib=0; ib<nb; ++ib )
        {
          const GridPosPoly& tb = bgp[ib];
          for ( Index ip=0; ip<np; ++ip )
            {
              const GridPosPoly& tp = pgp[ip];
              for ( Index ir=0; ir<nr; ++ir )
                {
                  const GridPosPoly& tr = rgp[ir];
                  for ( Index ic=0; ic<nc; ++ic )
                    {
                      const GridPosPoly& tc = cgp[ic];

                      Index iti = 0;

                      LOOPW(s)
                        LOOPW(b)
                        LOOPW(p)
                        LOOPW(r)
                        LOOPW(c)
                        {
                          itw(is,ib,ip,ir,ic,iti) =
                            (*s) * (*b) * (*p) * (*r) * (*c);
                          ++iti;
                        }
                    }
                }
            }
        }
    }
}

//! Compute 6D interpolation weights for an entire field.
/*! 
 Compute the weights for a "green" type interpolation of the field,
 that means that the grid position Arrays are interpreted as defining
 the grids for the interpolated field.

 The dimensions of itw must be consistent with this.

 Note that we still do not need the actual field for this step.

 This function can be easily distinguished from the other
 interpweights function (for "green" interpolation), because the
 output is a Tensor with one more dimension than the number of grid
 position Arrays.

 \retval itw Interpolation weights
 \param vgp The grid position Array for the vitrine dimension.
 \param sgp The grid position Array for the shelf   dimension.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.
 
 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interpweights( Tensor7View itw,
                    const ArrayOfGridPosPoly& vgp,
                    const ArrayOfGridPosPoly& sgp,
                    const ArrayOfGridPosPoly& bgp,
                    const ArrayOfGridPosPoly& pgp,
                    const ArrayOfGridPosPoly& rgp,
                    const ArrayOfGridPosPoly& cgp )
{
  Index nv = vgp.nelem();
  Index ns = sgp.nelem();
  Index nb = bgp.nelem();
  Index np = pgp.nelem();
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();

  assert(is_size(itw,nv,ns,nb,np,nr,nc,
                 vgp[0].w.nelem()*
                 sgp[0].w.nelem()*
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));    

  // We have to loop all the points in the new grid:
  for ( Index iv=0; iv<nv; ++iv )
    {
      const GridPosPoly& tv = vgp[iv];
      for ( Index is=0; is<ns; ++is )
        {
          const GridPosPoly& ts = sgp[is];
          for ( Index ib=0; ib<nb; ++ib )
            {
              const GridPosPoly& tb = bgp[ib];
              for ( Index ip=0; ip<np; ++ip )
                {
                  const GridPosPoly& tp = pgp[ip];
                  for ( Index ir=0; ir<nr; ++ir )
                    {
                      const GridPosPoly& tr = rgp[ir];
                      for ( Index ic=0; ic<nc; ++ic )
                        {
                          const GridPosPoly& tc = cgp[ic];

                          Index iti = 0;

                          LOOPW(v)
                            LOOPW(s)
                            LOOPW(b)
                            LOOPW(p)
                            LOOPW(r)
                            LOOPW(c)
                            {
                              itw(iv,is,ib,ip,ir,ic,iti) =
                                (*v) * (*s) * (*b) * (*p) * (*r) * (*c);
                              ++iti;
                            }
                        }
                    }
                }
            }
        }
    }
}

//! Interpolate 2D field to another 2D field.
/*! 
 This performs a "green" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining the grids
 for the interpolated field.

 This function can be easily distinguished from the other
 interpolation function (that creates a sequence of interpolated
 values), because of the dimension of ia and itw.

 The size of ia and itw in all dimensions must be consistent with the grid
 position Arrays.

 \retval ia  Interpolated field.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param rgp The grid position Array for the row dimension.
 \param cgp The grid position Array for the column dimension.
 
 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( MatrixView            ia,
             ConstTensor3View      itw,
             ConstMatrixView       a,   
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();
  assert(is_size(ia,nr,nc));    
  assert(is_size(itw,nr,nc,
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem())); 

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the new grid:
  for ( Index ir=0; ir<nr; ++ir )
    {
      // Current grid position:
      const GridPosPoly& tr = rgp[ir];

      for ( Index ic=0; ic<nc; ++ic )
        {
          // Current grid position:
          const GridPosPoly& tc = cgp[ic];

          // Get handle to current element of output tensor and initialize
          // it to zero:
          Numeric& tia = ia(ir,ic);
          tia = 0;

          Index iti = 0;
          LOOPIDX(r)
          LOOPIDX(c)
            {
              tia += a(*r,
                       *c) * itw(ir,ic,iti);
              ++iti;
            }
        }
    }
}

//! Interpolate 3D field to another 3D field.
/*! 
 This performs a "green" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining the grids
 for the interpolated field.

 This function can be easily distinguished from the other
 interpolation function (that creates a sequence of interpolated
 values), because of the dimension of ia and itw.

 The size of ia and itw in all dimensions must be consistent with the grid
 position Arrays.

 \retval ia  Interpolated field.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( Tensor3View           ia,
             ConstTensor4View      itw,
             ConstTensor3View      a,   
             const ArrayOfGridPosPoly& pgp,
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index np = pgp.nelem();
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();
  assert(is_size(ia,
                 np,nr,nc));    
  assert(is_size(itw,
                 np,nr,nc,
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,0,0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the new grid:
  for ( Index ip=0; ip<np; ++ip )
    {
      const GridPosPoly& tp = pgp[ip];
      for ( Index ir=0; ir<nr; ++ir )
        {
          const GridPosPoly& tr = rgp[ir];
          for ( Index ic=0; ic<nc; ++ic )
            {
              // Current grid position:
              const GridPosPoly& tc = cgp[ic];

              // Get handle to current element of output tensor and
              // initialize it to zero:
              Numeric& tia = ia(ip,ir,ic);
              tia = 0;

              Index iti = 0;
              LOOPIDX(p)
              LOOPIDX(r)
              LOOPIDX(c)
                    {
                      tia += a(*p,
                               *r,
                               *c) * itw(ip,ir,ic,
                                               iti);
                      ++iti;
                    }
            }
        }
    }
}

//! Interpolate 4D field to another 4D field.
/*! 
 This performs a "green" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining the grids
 for the interpolated field.

 This function can be easily distinguished from the other
 interpolation function (that creates a sequence of interpolated
 values), because of the dimension of ia and itw.

 The size of ia and itw in all dimensions must be consistent with the grid
 position Arrays.

 \retval ia  Interpolated field.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( Tensor4View           ia,
             ConstTensor5View      itw,
             ConstTensor4View      a,   
             const ArrayOfGridPosPoly& bgp,
             const ArrayOfGridPosPoly& pgp,
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index nb = bgp.nelem();
  Index np = pgp.nelem();
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();
  assert(is_size(ia,
                 nb,np,nr,nc));    
  assert(is_size(itw,
                 nb,np,nr,nc,
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,0,0,0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the new grid:
  for ( Index ib=0; ib<nb; ++ib )
    {
      const GridPosPoly& tb = bgp[ib];
      for ( Index ip=0; ip<np; ++ip )
        {
          const GridPosPoly& tp = pgp[ip];
          for ( Index ir=0; ir<nr; ++ir )
            {
              const GridPosPoly& tr = rgp[ir];
              for ( Index ic=0; ic<nc; ++ic )
                {
                  // Current grid position:
                  const GridPosPoly& tc = cgp[ic];

                  // Get handle to current element of output tensor and
                  // initialize it to zero:
                  Numeric& tia = ia(ib,ip,ir,ic);
                  tia = 0;

                  Index iti = 0;
                  LOOPIDX(b)
                  LOOPIDX(p)
                  LOOPIDX(r)
                  LOOPIDX(c)
                          {
                            tia += a(*b,
                                     *p,
                                     *r,
                                     *c) * itw(ib,ip,ir,ic,
                                                     iti);
                            ++iti;
                          }
                }
            }
        }
    }
}

//! Interpolate 5D field to another 5D field.
/*! 
 This performs a "green" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining the grids
 for the interpolated field.

 This function can be easily distinguished from the other
 interpolation function (that creates a sequence of interpolated
 values), because of the dimension of ia and itw.

 The size of ia and itw in all dimensions must be consistent with the grid
 position Arrays.

 \retval ia  Interpolated field.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param sgp The grid position Array for the shelf   dimension.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( Tensor5View           ia,
             ConstTensor6View      itw,
             ConstTensor5View      a,   
             const ArrayOfGridPosPoly& sgp,
             const ArrayOfGridPosPoly& bgp,
             const ArrayOfGridPosPoly& pgp,
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index ns = sgp.nelem();
  Index nb = bgp.nelem();
  Index np = pgp.nelem();
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();
  assert(is_size(ia,
                 ns,nb,np,nr,nc));    
  assert(is_size(itw,
                 ns,nb,np,nr,nc,
                 sgp[0].w.nelem()*
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,0,0,0,0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the new grid:
  for ( Index is=0; is<ns; ++is )
    {
      const GridPosPoly& ts = sgp[is];
      for ( Index ib=0; ib<nb; ++ib )
        {
          const GridPosPoly& tb = bgp[ib];
          for ( Index ip=0; ip<np; ++ip )
            {
              const GridPosPoly& tp = pgp[ip];
              for ( Index ir=0; ir<nr; ++ir )
                {
                  const GridPosPoly& tr = rgp[ir];
                  for ( Index ic=0; ic<nc; ++ic )
                    {
                      // Current grid position:
                      const GridPosPoly& tc = cgp[ic];

                      // Get handle to current element of output tensor and
                      // initialize it to zero:
                      Numeric& tia = ia(is,ib,ip,ir,ic);
                      tia = 0;

                      Index iti = 0;
                      LOOPIDX(s)
                      LOOPIDX(b)
                      LOOPIDX(p)
                      LOOPIDX(r)
                      LOOPIDX(c)
                                {
                                  tia += a(*s,
                                           *b,
                                           *p,
                                           *r,
                                           *c) * itw(is,ib,ip,ir,ic,
                                                           iti);
                                  ++iti;
                                }
                    }
                }
            }
        }
    }
}

//! Interpolate 6D field to another 6D field.
/*! 
 This performs a "green" type interpolation of the field, that means
 that the grid position Arrays are interpreted as defining the grids
 for the interpolated field.

 This function can be easily distinguished from the other
 interpolation function (that creates a sequence of interpolated
 values), because of the dimension of ia and itw.

 The size of ia and itw in all dimensions must be consistent with the grid
 position Arrays.

 \retval ia  Interpolated field.
 \param itw Interpolation weights.
 \param a   The field to interpolate.
 \param vgp The grid position Array for the vitrine dimension.
 \param sgp The grid position Array for the shelf   dimension.
 \param bgp The grid position Array for the book    dimension.
 \param pgp The grid position Array for the page    dimension.
 \param rgp The grid position Array for the row     dimension.
 \param cgp The grid position Array for the column  dimension.

 \author Stefan Buehler <sbuehler(at)ltu.se>
 \date   2008-03-06
*/
void interp( Tensor6View           ia,
             ConstTensor7View      itw,
             ConstTensor6View      a,   
             const ArrayOfGridPosPoly& vgp,
             const ArrayOfGridPosPoly& sgp,
             const ArrayOfGridPosPoly& bgp,
             const ArrayOfGridPosPoly& pgp,
             const ArrayOfGridPosPoly& rgp,
             const ArrayOfGridPosPoly& cgp)
{
  Index nv = vgp.nelem();
  Index ns = sgp.nelem();
  Index nb = bgp.nelem();
  Index np = pgp.nelem();
  Index nr = rgp.nelem();
  Index nc = cgp.nelem();
  assert(is_size(ia,
                 nv,ns,nb,np,nr,nc));    
  assert(is_size(itw,
                 nv,ns,nb,np,nr,nc,
                 vgp[0].w.nelem()*
                 sgp[0].w.nelem()*
                 bgp[0].w.nelem()*
                 pgp[0].w.nelem()*
                 rgp[0].w.nelem()*
                 cgp[0].w.nelem()));

  // Check that interpolation weights are valid. The sum of all
  // weights (last dimension) must always be approximately one. We
  // only check the first element.
  assert( is_same_within_epsilon( itw(0,0,0,0,0,0,Range(joker)).sum(),
                                  1,
                                  sum_check_epsilon ) );
  
  // We have to loop all the points in the new grid:
  for ( Index iv=0; iv<nv; ++iv )
    {
      const GridPosPoly& tv = vgp[iv];
      for ( Index is=0; is<ns; ++is )
        {
          const GridPosPoly& ts = sgp[is];
          for ( Index ib=0; ib<nb; ++ib )
            {
              const GridPosPoly& tb = bgp[ib];
              for ( Index ip=0; ip<np; ++ip )
                {
                  const GridPosPoly& tp = pgp[ip];
                  for ( Index ir=0; ir<nr; ++ir )
                    {
                      const GridPosPoly& tr = rgp[ir];
                      for ( Index ic=0; ic<nc; ++ic )
                        {
                          // Current grid position:
                          const GridPosPoly& tc = cgp[ic];

                          // Get handle to current element of output tensor and
                          // initialize it to zero:
                          Numeric& tia = ia(iv,is,ib,ip,ir,ic);
                          tia = 0;

                          Index iti = 0;
                          LOOPIDX(v)
                          LOOPIDX(s)
                          LOOPIDX(b)
                          LOOPIDX(p)
                          LOOPIDX(r)
                          LOOPIDX(c)
                                      {
                                        tia += a(*v,
                                                 *s,
                                                 *b,
                                                 *p,
                                                 *r,
                                                 *c) * itw(iv,is,ib,ip,ir,ic,
                                                                 iti);
                                        ++iti;
                                      }
                        }
                    }
                }
            }
        }
    }
}


