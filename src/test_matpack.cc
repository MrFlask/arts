/* Copyright (C) 2001 Stefan Buehler <sbuehler@uni-bremen.de>

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

#include "matpackI.h"
#include "matpackIII.h"
#include "array.h"
#include "make_array.h"
#include "mystring.h"
#include "make_vector.h"
#include "math_funcs.h"


/** Define the global joker objekt. */
Joker joker;

Numeric by_reference(const Numeric& x)
{
  return x+1;
}

Numeric by_value(Numeric x)
{
  return x+1;
}

void fill_with_junk(VectorView x)
{
  x = 999;
}

void fill_with_junk(MatrixView x)
{
  x = 888;
}

int test1()
{
  Vector v(20);

  cout << "v.nelem() = " << v.nelem() << "\n";

  for (Index i=0; i<v.nelem(); ++i )
    v[i] = i;

  cout << "v.begin() = " << *v.begin() << "\n";

  cout << "v = \n" << v << "\n";

  fill_with_junk(v[Range(1,8,2)][Range(2,joker)]);
  //  fill_with_junk(v);

  Vector v2 = v[Range(2,4)];

  cout << "v2 = \n" << v2 << "\n";

  for (Index i=0 ; i<1000; ++i)
    {
      Vector v3(1000);
      v3 = i;
    }

  v2[Range(joker)] = 88;

  v2[Range(0,2)] = 77;

  cout << "v = \n" << v << "\n";
  cout << "v2 = \n" << v2 << "\n";
  cout << "v2.nelem() = \n" << v2.nelem() << "\n";

  Vector v3;
  v3.resize(v2.nelem());
  v3 = v2;

  cout << "\nv3 = \n" << v3 << "\n";
  fill_with_junk(v2);
  cout << "\nv3 after junking v2 = \n" << v3 << "\n";
  v3 *= 2;
  cout << "\nv3 after *2 = \n" << v3 << "\n";

  Matrix M(10,15);
  {
    Numeric n=0;
    for (Index i=0; i<M.nrows(); ++i)
      for (Index j=0; j<M.ncols(); ++j)
	M(i,j) = ++n;
  }

  cout << "\nM =\n" << M << "\n";

  cout << "\nM(Range(2,4),Range(2,4)) =\n" << M(Range(2,4),Range(2,4)) << "\n";

  cout << "\nM(Range(2,4),Range(2,4))(Range(1,2),Range(1,2)) =\n"
       << M(Range(2,4),Range(2,4))(Range(1,2),Range(1,2)) << "\n";

  cout << "\nM(1,Range(joker)) =\n" << M(1,Range(joker)) << "\n";

  cout << "\nFilling M(1,Range(1,2)) with junk.\n";
  fill_with_junk(M(1,Range(1,2)));
    
  cout << "\nM(Range(0,4),Range(0,4)) =\n" << M(Range(0,4),Range(0,4)) << "\n";

  cout << "\nFilling M(Range(4,2,2),Range(6,3)) with junk.\n";

  MatrixView s = M(Range(4,2,2),Range(6,3));
  fill_with_junk(s);

  cout << "\nM =\n" << M << "\n";

  const Matrix C = M;

  cout << "\nC(Range(3,4,2),Range(2,3,3)) =\n"
       << C(Range(3,4,2),Range(2,3,3)) << "\n";

  cout << "\nC(Range(3,4,2),Range(2,3,3)).transpose() =\n"
       << transpose(C(Range(3,4,2),Range(2,3,3))) << "\n";

  return 0;
}

void test2()
{
  Vector v(50000000);

  cout << "v.nelem() = " << v.nelem() << "\n";

  cout << "Filling\n";
//   for (Index i=0; i<v.nelem(); ++i )
//     v[i] = sqrt(i);
  v = 1.;
  cout << "Done\n";

}


// void test3()
// {
//   SparseMatrix M(10,15);

//   cout << "M.nrows(), M.ncols() = "
//        << M.nrows() << ", " << M.ncols() << "\n";

//   for (Index i=0; i<10; ++i)
//     M(i,i) = i+1;

//   cout << "\nM = \n" << M;

//   const SparseMatrix S(M);

//   cout << "\nS(2,0) = " << S(2,0) << "\n";

//   cout << "\nS = \n" << S;

// }

void test4()
{
  Vector a(10);
  Vector b(a.nelem());
  
  for ( Index i=0; i<a.nelem(); ++i )
    {
      a[i] = i+1;
      b[i] = a.nelem()-i;
    }

  cout << "a = \n" << a << "\n";
  cout << "b = \n" << b << "\n";
  cout << "a*b \n= " << a*b << "\n";

  Matrix A(11,6);
  Matrix B(10,20);
  Matrix C(20,5);

  B = 2;
  C = 3;
  mult(A(Range(1,joker),Range(1,joker)),B,C);

  //  cout << "\nB =\n" << B << "\n";
  //  cout << "\nC =\n" << C << "\n";
  cout << "\nB*C =\n" << A << "\n";
  
}

void test5()
{
  Vector a(10);
  Vector b(20);
  Matrix M(10,20);

  // Fill b and M with a constant number:
  b = 1;
  M = 2;

  cout << "b = \n" << b << "\n";
  cout << "M =\n" << M << "\n";

  mult(a,M,b);    // a = M*b
  cout << "\na = M*b = \n" << a << "\n";

  mult(transpose(b),transpose(a),M);    // b^t = a^t * M
  cout << "\nb^t = a^t * M = \n" <<  transpose(b) << "\n";
  
}

void test6()
{
  Index n = 5000;
  Vector x(1,n,1), y(n);
  Matrix M(n,n);
  M = 1;
  //  cout << "x = \n" << x << "\n";

  cout << "Transforming.\n";
  //  transform(x,sin,x);
  // transform(transpose(y),sin,transpose(x));
  //  cout << "sin(x) =\n" << y << "\n";
  for (Index i=0; i<1000; ++i)
    {
      //      mult(y,M,x);
      transform(y,sin,static_cast<MatrixView>(x));
      x+=1;
    }
  //  cout << "y =\n" << y << "\n";
  
  cout << "Done.\n";
}

void test7()
{
  Vector x(1,20000000,1);
  Vector y(x.nelem());
  transform(y,sin,x);
  cout << "min(sin(x)), max(sin(x)) = " << min(y) << ", " << max(y) << "\n";
}

void test8()
{
  Vector x(80000000);
  for ( Index i=0; i<x.nelem(); ++i )
    x[i] = i;
  cout << "Done." << "\n";
}

void test9()
{
  // Initialization of Matrix with view of other Matrix:
  Matrix A(4,8);
  Matrix B(A(Range(joker),Range(0,3)));
  cout << "B = " << B << "\n";
}

void test10()
{
  // Initialization of Matrix with a vector (giving a 1 column Matrix).

  // At the moment doing this with a non-const Vector will result in a
  // warning message. 
  Vector v(1,8,1);
  Matrix M(static_cast<const Vector>(v));
  cout << "M = " << M << "\n";
}

void test11()
{
  // Assignment between Vector and Matrix:

  // At the moment doing this with a non-const Vector will result in a
  // warning message. 
  Vector v(1,8,1);
  Matrix M(v.nelem(),1);
  M = v;
  cout << "M = " << M << "\n";
}

void test12()
{
  // Copying of Arrays

  Array<String> sa(3);
  sa[0] = "It's ";
  sa[1] = "a ";
  sa[2] = "test.";

  Array<String> sb(sa), sc(sa.nelem());

  cout << "sb = \n" << sb << "\n";

  sc = sa;

  cout << "sc = \n" << sc << "\n";

}

void test13()
{
  // Mix vector and one-column matrix in += operator.
  const Vector v(1,8,1);	// The const is necessary here to
				// avoid compiler warnings about
				// different conversion paths.
  Matrix M(v);
  M += v;
  cout << "M = \n" << M << "\n";
}

void test14()
{
  // Test explicit Array constructors.
  Array<String> a = MakeArray<String>("Test");
  Array<Index>  b = MakeArray<Index>(1,2);
  Array<Numeric> c = MakeArray<Numeric>(1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0);
  cout << "a = \n" << a << "\n";
  cout << "b = \n" << b << "\n";
  cout << "c = \n" << c << "\n";
}

void test15()
{
  // Test String.
  String a = "Nur ein Test.";
  cout << "a = " << a << "\n";
  String b(a,5,-1);
  cout << "b = " << b << "\n";
}

void test16()
{
  // Test interaction between Array<Numeric> and Vector.
  Vector a;
  Array<Numeric> b;
  b.push_back(1);
  b.push_back(2);
  b.push_back(3);
  a.resize(b.nelem());
  a = b;
  cout << "b =\n" << b << "\n";
  cout << "a =\n" << a << "\n";
}

void test17()
{
  // Test Sum.
  Vector a(1,10,1);
  cout << "a.sum() = " << a.sum() << "\n";
}

void test18()
{
  // Test elementvise square of a vector:
  Vector a(1,10,1);
  a *= a;
  cout << "a *= a =\n" << a << "\n";
}

void test19()
{
  // There exists no explicit filling constructor of the form 
  // Vector a(3,1.7).
  // But you can use the more general filling constructor with 3 arguments.

  Vector a(1,10,1);
  Vector b(5.3,10,0);
  cout << "a =\n" << a << "\n";
  cout << "b =\n" << b << "\n";
}

void test20()
{
  // Test MakeVector:
  MakeVector a(1,2,3,4,5,6,7,8,9,10);
  cout << "a =\n" << a << "\n";
}

void test21()
{
  Numeric s=0;
  // Test speed of call by reference:
  cout << "By reference:\n";
  for ( Index i=0; i<1e8; ++i )
    {
      s += by_reference(s);
      s -= by_reference(s);
    }
  cout << "s = " << s << "\n";  
}

void test22()
{
  Numeric s=0;
  // Test speed of call by value:
  cout << "By value:\n";
  for ( Index i=0; i<1e8; ++i )
    {
      s += by_value(s);
      s -= by_value(s);
    }
  cout << "s = " << s << "\n";  
}

void test23()
{
  // Test constructors that fills with constant:
  Vector a(10,3.5);
  cout << "a =\n" << a << "\n";
  Matrix b(10,10,4.5);
  cout << "b =\n" << b << "\n";
}

void test24()
{
  // Try element-vise multiplication of Matrix and Vector:
  Matrix a(5,1,2.5);
  Vector b(1,5,1);
  a *= b;
  cout << "a*=b =\n" << a << "\n";
  a /= b;
  cout << "a/=b =\n" << a << "\n";
  a += b;
  cout << "a+=b =\n" << a << "\n";
  a -= b;
  cout << "a-=b =\n" << a << "\n";
}

void test25()
{
  // Test min and max for Array:
  MakeArray<Index> a(1,2,3,4,5,6,5,4,3,2,1);
  cout << "min/max of a = " << min(a) << "/" << max(a) << "\n";
}

void test26()
{
  cout << "Test filling constructor for Array:\n";
  Array<String> a(4,"Hello");
  cout << "a =\n" << a << "\n";
}

void test27()
{
  cout << "Test Arrays of Vectors:\n";
  Array<Vector> a;
  a.push_back(MakeVector(1.0,2.0));
  a.push_back(Vector(1.0,10,1.0));
  cout << "a =\n" << a << "\n";
}

void test28()
{
  cout << "Test default constructor for Matrix:\n";
  Matrix a;
  Matrix b(a);
  cout << "b =\n" << b << "\n";
}

void test29()
{
  cout << "Test Arrays of Matrix:\n";
  ArrayOfMatrix a;
  Matrix b;

  b.resize(2,2);
  b(0,0) = 1;
  b(0,1) = 2;
  b(1,0) = 3;
  b(1,1) = 4;
  a.push_back(b);
  b *= 2;
  a.push_back(b);

  a[0].resize(2,3);
  a[0] = 4;

  a.resize(3);
  a[2].resize(4,5);
  a[2] = 5;

  cout << "a =\n" << a << "\n";
}

void test30()
{
  cout << "Test Matrices of size 0:\n";
  Matrix a(0,0);
  //  cout << "a(0,0) =\n" << a(0,0) << "\n";
  a.resize(2,2);
  a = 1;
  cout << "a =\n" << a << "\n";

  Matrix b(3,0);
  //  cout << "b(0,0) =\n" << b(0,0) << "\n";
  b.resize(b.nrows(),b.ncols()+3);
  b = 2;
  cout << "b =\n" << b << "\n";

  Matrix c(0,3);
  //  cout << "c(0,0) =\n" << c(0,0) << "\n";
  c.resize(c.nrows()+3,c.ncols());
  c = 3;
  cout << "c =\n" << c << "\n";
}

void test31()
{
  cout << "Test Tensor3:\n";

  Tensor3 a(2,3,4,1.0);

  Index fill = 0;

  // Fill with some numbers
  for ( Index i=0; i<a.npages(); ++i )
    for ( Index j=0; j<a.nrows(); ++j )
      for ( Index k=0; k<a.ncols(); ++k )
	a(i,j,k) = ++fill;

  cout << "a =\n" << a << "\n";

  cout << "Taking out first row of first page:\n"
       << a(0,0,Range(joker)) << "\n";

  cout << "Taking out last column of second page:\n"
       << a(1,Range(joker),a.ncols()-1) << "\n";

  cout << "Taking out the first letter on every page:\n"
       << a(Range(joker),0,0) << "\n";

  cout << "Taking out first page:\n"
       << a(0,Range(joker),Range(joker)) << "\n";

  cout << "Taking out last row of all pages:\n"
       << a(Range(joker),a.nrows()-1,Range(joker)) << "\n";

  cout << "Taking out second column of all pages:\n"
       << a(Range(joker),Range(joker),1) << "\n";

  a *= 2;

  cout << "After element-vise multiplication with 2:\n"
       << a << "\n";

  transform(a,sqrt,a);

  cout << "After taking the square-root:\n"
       << a << "\n";

  Index s = 200;
  cout << "Let's allocate a large tensor, "
       << s*s*s*8/1024./1024.
       << " MB...\n";

  a.resize(s,s,s);

  cout << "Set it to 1...\n";

  a = 1;

  cout << "a(900,900,900) = " << a(90,90,90) << "\n";

  fill = 0;

  cout << "Fill with running numbers, using for loops...\n";
  for ( Index i=0; i<a.npages(); ++i )
    for ( Index j=0; j<a.nrows(); ++j )
      for ( Index k=0; k<a.ncols(); ++k )
	a(i,j,k) = ++fill;

  cout << "Max(a) = ...\n";

  cout << max(a) << "\n";

}

int main()
{
  test31();
  return 0;
}
