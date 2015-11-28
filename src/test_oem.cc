/*!
  \file   test_oem.cc
  \author Simon Pfreundschuh <simon@thinks>
  \date   Sat Apr 18 19:50:30 2015

  \brief  Test for the OEM functions.
*/


#include <cmath>
#include "engine.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include "lin_alg.h"
#include "matrix.h"
#include "oem.h"
#include <stdlib.h>
#include <string>
#include "test_utils.h"
#include <time.h>
#include "unistd.h"

using std::abs;
using std::cout;
using std::endl;
using std::min;
using std::ofstream;
using std::setw;
using std::string;

string source_dir = SOURCEDIR;
string atmlab_dir = ATMLABDIR;

// Forward declarations.

void write_matrix( ConstMatrixView A, const char* fname );

//! Linear Forward model.
/*!

  Linear forward model class that represents an affine relationship
  between state vector x and measurement vector y:

      y = K * x + y_0

  The resulting object stores a copy of the jacobian and the offset vector
  as Matrix and Vector, respectively.

*/
class LinearModel : public ForwardModel
{

public:

    /** Default Constructor */
    LinearModel() {}

    /** Construct a linear model from a given Jacobian J and offset vector
        y0.

        \param[in] J_ The Jacobian of the forward model.
        \param[in] y0_ The constant offset of the forward model.
    */
    LinearModel( ConstMatrixView J_,
                 ConstVectorView y0_ ) : J( J_ ), y0( y0_ ) {}

    /** See ForwardModel class. */
    void evaluate_jacobian( VectorView &yi,
                            MatrixView &Ki,
                            const ConstVectorView &xi )
        {
            Ki = J;
            mult( yi, Ki, xi );
            yi += y0;
        }

    /** See ForwardModel class. */
    void evaluate( VectorView &yi,
                   const ConstVectorView &xi )
        {
            mult( yi, J, xi );
            yi += y0;
        }

private:

    Matrix J;
    Vector y0;

};

//! Quadratic Forward Model
/*!
  Test class for the ForwardModel class as defined in oem.h. Implements
  a quadratic length-m vector valued function in n variables. The function
  is represented by a set of m-hessians and a Jacobian. On construction those
  are set to random matrices to test the non-linear OEM methods.
 */
class QuadraticModel : public ForwardModel
{
public:

    //! Constructor.
    /*!
      Construct a random quadratic model. Allocates the necessary space
      and fille the Jacobian with values in the range [-10,10] and the Hessians
      in the range [0,0.1]. Also writes all matrices to text files for the
      communication with the Matlab process.

      \param[in] m_ The dimension of the measurement space.
      \param[in] n_ The dimension of the state space.
    */
    QuadraticModel( Index m_, Index n_ )
        {

            m = m_;
            n = n_;
            char fname[40];

            Jacobian = Matrix(m, n, 0);
            Hessians = new Matrix[m];
            random_fill_matrix( Jacobian, 1.0, false );
            sprintf( fname, "J_t.txt" );
            write_matrix( Jacobian, fname );

            for ( Index i = 0; i < m; i++ )
            {
                Hessians[i] = Matrix( n, n, 0 );
                random_fill_matrix_pos_semi_def(Hessians[i], 1, true);
                sprintf( fname, "H_%d_t.txt", (int) i);
                write_matrix( Hessians[i], fname );
            }

        }

    //! Destructor.
    ~QuadraticModel()
        {
            delete [] Hessians;
        }

    //! Virtual function of the FowardModel class.
    void evaluate_jacobian( VectorView &yi,
                            MatrixView &Ki,
                            const ConstVectorView &xi )
        {

            for ( Index i = 0; i < m; i++ )
            {
                mult( Ki( i, Joker()), Hessians[i], xi );
            }

            Ki *= 0.5;
            Ki += Jacobian;
            mult( yi, Ki, xi );

        }

    //! Virtual function of the FowardModel class.
    void evaluate( VectorView& yi,
                   const ConstVectorView& xi )
        {

            Matrix Ki( m,n );

            for ( Index i = 0; i < n; i++ )
            {
                mult( Ki( i, Joker()), Hessians[i], xi );
            }

            Ki *= 0.5;
            Ki += Jacobian;
            mult( yi, Ki, xi );

        }

private:

    Index m,n;
    Matrix Jacobian;
    Matrix* Hessians;

};

//! Write matrix to text file.
/*!

  Write the given matrix in plain text to the file filename in the
  current directory.
  \param[in] A The matrix to write to the file.
  \param[in] filename The name of the file to write to.
*/
void write_matrix( ConstMatrixView A,
                   const char* filename )
{

    Index m = A.nrows();
    Index n = A.ncols();

    ofstream ofs( filename, ofstream::out);

    for (Index i = 0; i < m; i++)
    {
        for (Index j = 0; j < (n - 1); j++)
        {
            ofs << std::setprecision(40) << A(i,j) << " ";
        }
        ofs << A( i, n - 1 );
        ofs << endl;
    }
    ofs.close();
}

//! Write vector to text file.
/*!
  Write the given vector to the file filename in the current directory.
  \param[in] v The vector to write.
  \param[in] filename The name of the file to write to.
*/void write_vector( ConstVectorView v,
                   const char* filename )
{
    Index n = v.nelem();

    ofstream ofs( filename, ofstream::out);

    for ( Index i=0; i<n; i++)
    {
        ofs << std::setprecision(20) << v[i] << endl;
    }
    ofs.close();
}

//! Generate test data for linear OEM retrieval.
/*!
  Fills the given matrices and vectors needed for the linear OEM retrieval
  functions with random values.

  \param y The measurement vector. Filled with random integer values in the range
           [0, 10].
  \param xa The a priori vector. Filled with random integer values in the range
           [0, 10].
  \param K The linear forward model. Filled with random integer values in the
           range [-10, 10].
  \param Se The covariance matrix for observation uncertainties. Filled with
            random values in the range [-1, 1] (Scaled down from the range
            [-10, 10]).
  \param Sa The covariance matrix for a priori uncertainties. Filled with random
            values in the range [-1, 1] (Scaled down from the range [-10, 10]).
*/
void generate_test_data( VectorView y,
                         VectorView xa,
                         MatrixView Se,
                         MatrixView Sx )
{

    random_fill_vector( y, 10, false );
    random_fill_vector( xa, 10, false );

    random_fill_matrix( Se, 1.0, false);
    Matrix tmp( Se );
    // Make sure Se is positive semi-definite.
    mult( Se, transpose( tmp ), tmp );

    random_fill_matrix_symmetric( Sx, 1.0, false);
    tmp = Sx;
    // Make sure Sx is positive semi-definite.
    mult( Sx, transpose( tmp ), tmp );

}

//! Generate linear forward model.
/*!

  Fills the given matrix K with random values in the range [-10,10].

  \param[in,out] K The matrix representing the forward model.
*/
void generate_linear_model( MatrixView K )
{
    random_fill_matrix( K, 10, false );
}

//! Run test script in matlab.
/*!
 Runs the test script given by filename in matlab. Reads out the
 the execution time from the variable t in the current workspace and
 returns is as return value.

  \param[in] eng Pointer to the running matlab engine.
  \param[in] filename Name of the file to be run.

  \return The time required for the execution.
*/
Index run_test_matlab( Engine* eng,
                       string filename )
{

    mxArray* t;
    Index time;

    // Run test.
    string cmd = "run('" + filename + "');";
    engEvalString( eng, cmd.c_str() );

    // Get execution time from matlab.
    t = engGetVariable( eng, "t" );
    time = (Index) ((Numeric*) mxGetData( t ))[0];
    return time;

}

//! Run test script in matlab and return result vector.
/*!
  Runs the oem function from the atmlab package. Runs the given external matlab
  script. The results of the computation are read from the Matlab workspace
  variables x and t and returned in the x vector argument and the return value.

  \param[out] x Vector to write the results of the retrieval to.
  \param[in] eng Pointer to the matlab engine that manages the running Matlab
                 session.
  \param[in] filename Name of the test script to be run.

  \return Execution time of oem in matlab in ms.
*/
Index run_oem_matlab( VectorView x,
                      MatrixView G,
                      Engine* eng,
                      string filename )
{
    Index n = G.nrows();
    Index m = G.ncols();
    Index time;
    mxArray *x_m, *G_m, *t;

    // Run test.
    string cmd = "run('" + filename + "');";
    engEvalString( eng, cmd.c_str() );

    // Read out results.
    x_m = engGetVariable( eng, "x" );
    G_m = engGetVariable( eng, "G" );

    for ( Index i = 0; i < n; i++ )
    {
        x[i] = ((Numeric*) mxGetData( x_m ))[i];

        for (Index j = 0; j < m; j++ )
        {
            G(i,j) = ((Numeric*) mxGetData( G_m ))[ j*n + i];
        }
    }

    // Get execution time from matlab.
    t = engGetVariable( eng, "t" );
    time = (Index) ((Numeric*) mxGetData( t ))[0];
    return time;
}


//! Setup the test environment.
/*!
  Changes to the test directory and initializes the matlab engine. Initialized
  the atmlab package.

  \param[in,out] eng Pointer variable that will contain the pointer to the
                     initialized Matlab engine.
*/
void setup_test_environment( Engine * &eng )
{
    // swith to test folder
    string cmd;
    cmd = source_dir + "/test_oem_files";
    int out = chdir( cmd.c_str() );
    (void) out;

    // Start MATLAB and try to initialize atmlab package.
    string atmlab_init = "run('" + atmlab_dir + "/atmlab/atmlab_init.m');";

    eng = engOpen(NULL);

    engEvalString( eng, atmlab_init.c_str() );
    cmd = "cd('" + source_dir + "/test_oem_files');";
    engEvalString( eng, cmd.c_str() );
}

//! Plot benchmark results
/*!

  Run matlab script that generates a plot of the benchmark results.

  \param[in] eng Pointer to the running matlab engine.
  \param[in] filename Filename of the file containing the results. Should be
                      'times_mult.txt' or 'times_linear.txt'.
*/
void run_plot_script( Engine *eng,
                      string filename,
                      string title )
{

    string cmd = "filename = '" + filename + "'";
    engEvalString( eng, cmd.c_str() );
    cmd = "plot_title = '" + title + "'";
    engEvalString( eng, cmd.c_str() );
    engEvalString( eng, "run('make_plot.m');" );

}

//! Tidy up test environment
/*!
  Deletes temporary test files and closes the Matlab session.
  \param[in] eng Pointer to the running Matlab engine.
*/
void tidy_up_test_environment( Engine *eng)
{
    int out = system( "rm *_t.txt" );
    (void) out;

    engEvalString( eng, "close()" );
}

//! Matrix inversion benchmark.
/*!

  Inverts randomly generated matrix matrices in matlab and in arts and compares
  the performance. Performs ntests numbers of test with matrices linearly
  increasing in size starting at n0 and ending an n1. Writes the result to
  standard out and to the file "times_inv.txt" in the current directory. Also
  generates a plot of the data using matlab stored in "times_inv.png".

  \param[in] n0 Size of the smallest matrix in the benchmark.
  \param[in] n1 Size of the largest matrix in the benchmark.
  \param[in] ntests Number of tests to be performed. In each step the size
                    of the matrix is linearly increased from n0 to n1.
*/
void benchmark_inv( Engine* eng,
                    Index n0,
                    Index n1,
                    Index ntests )
{
    Index step = (n1 - n0) / (ntests - 1);
    Index n = n0;

    ofstream ofs( "times_inv.txt", ofstream::out );
    ofs << "#" << setw(4) << "n" << setw(10) << "BLAS";
    ofs << setw(10) << "arts" << setw(10) << "Matlab" << endl;

    cout << endl << "N TIMES N MATRIX INVERSION" << endl << endl;
    cout << setw(5) << "n" << setw(10) << "BLAS" << setw(10);
    cout << setw(10) << "arts" << setw(10) << "Matlab" << endl;

    for ( Index i = 0; i < ntests; i++ )
    {
        Matrix A(n,n), B(n,n);

        random_fill_matrix( A, 100, false );
        write_matrix( A, "A_t.txt");

        Index t, t1, t2, t_blas, t1_blas, t2_blas, t_m;

        t1 = clock();
        inv( B, A );
        t2 = clock();
        t = (t2 - t1) * 1000 / CLOCKS_PER_SEC;

        t1_blas = clock();
        inv( B, A );
        t2_blas = clock();
        t_blas = (t2_blas - t1_blas) * 1000 / CLOCKS_PER_SEC;

        t_m = run_test_matlab( eng, "test_inv.m" );

        ofs << setw(5) << n << setw(10) << t_blas << setw(10);
        ofs << t << setw(10) << t_m << endl;
        cout << setw(5) << n << setw(10) << t_blas << setw(10);
        cout << t << setw(10) << t_m << endl;

        n += step;
    }
    cout << endl << endl;

    // Tidy up
    ofs.close();
    run_plot_script( eng, "times_inv.txt", "Matrix Inversion" );

}

//! Matrix multiplication benchmark.
/*!

  Multiplies two identical matrices in matlab and in arts and compares
  the performance. Performs ntests numbers of test with matrices linearly
  increasing in size starting at n0 and ending an n1. Writes the result to
  standard out and to the file "times_mult.txt" in the current directory. Also
  generates a plot of the data using matlab stored in "times_mult.png".

  \param[in] n0 Size of the smallest matrix in the benchmark.
  \param[in] n1 Size of the largest matrix in the benchmark.
  \param[in] ntests Number of tests to be performed. In each step the size
                    of the matrix is linearly increased from n0 to n1.
*/
void benchmark_mult( Engine* eng,
                     Index n0,
                     Index n1,
                     Index ntests )
{
    Index step = (n1 - n0) / (ntests - 1);
    Index n = n0;

    ofstream ofs( "times_mult.txt", ofstream::out );
    ofs << "#" << setw(4) << "n" << setw(10) << "BLAS";
    ofs << setw(10) << "arts" << setw(10) << "Matlab" << endl;

    cout << endl << "N TIMES N MATRIX MULTIPLICATION" << endl << endl;
    cout << setw(5) << "n" << setw(10) << "BLAS" << setw(10);
    cout << setw(10) << "arts" << setw(10) << "Matlab" << endl;

    for ( Index i = 0; i < ntests; i++ )
    {
        Matrix A(n,n), B(n,n);

        random_fill_matrix( A, 100, false );
        write_matrix( A, "A_t.txt");

        Index t, t1, t2, t_blas, t1_blas, t2_blas, t_m;

        t1 = clock();
        mult_general( B, A, A );
        t2 = clock();
        t = (t2 - t1) * 1000 / CLOCKS_PER_SEC;

        t1_blas = clock();
        mult( B, A, A );
        t2_blas = clock();
        t_blas = (t2_blas - t1_blas) * 1000 / CLOCKS_PER_SEC;

        t_m = run_test_matlab( eng, "test_mult.m" );

        ofs << setw(5) << n << setw(10) << t_blas << setw(10) << t << setw(10);
        ofs << t_m << endl;
        cout << setw(5) << n << setw(10) << t_blas << setw(10) << t << setw(10);
        cout << t_m << endl;

        n += step;
    }
    cout << endl << endl;

    // Tidy up
    ofs.close();
    run_plot_script( eng, "times_mult.txt", "Matrix Multiplication" );

}

//! Benchmark linear oem.
/*!
  Run linear oem test and benchmark. Runs ntests numbers of tests with the size
  of K growing linearly from n0 to n1. Prints for each test the maximum relative
  error in the result compared to the matlab implementation and the cpu time in
  milliseconds needed for the execution. The timing results are also written to
  to a file "times_linear.txt" in the current directory.

  \param n0 Starting size for the K matrix.
  \param n1 Final size for the K matrix.
  \param ntests Number of tests to be performed.
*/
void benchmark_oem_linear( Engine* eng,
                           Index n0,
                           Index n1,
                           Index ntests )
{
    Index step = (n1 - n0) / (ntests - 1);
    Index n = n0;

    ofstream ofs( "times_linear.txt", ofstream::out );

    ofs << "#" << setw(4) << "n" << setw(10) << "Matlab";
    ofs << setw(10) << "C++" << endl;

    cout << endl << "LINEAR OEM" << endl << endl;
    cout << setw(5) << "n" << setw(10) << "C++" << setw(10);
    cout << "Matlab" << setw(20) << "Max. Rel. Error" << endl;


    // Run tests.
    for ( Index i = 0; i < ntests; i++ )
    {
        Vector x_nform(n), x_mform(n), x_m(n), y(n), yf(n), xa(n), x_norm(n),
            zero(n);
        Matrix J(n,n), Se(n,n), Sa(n,n), SeInv(n,n), SxInv(n,n), G_nform(n,n),
            G_mform(n,n), G_m(n,n);

        zero = 0.0;

        generate_test_data( y, xa, Se, Sa );
        generate_linear_model( J );
        LinearModel K( J, zero );

        write_vector( xa, "xa_t.txt" );
        write_vector( y, "y_t.txt" );
        write_matrix( J, "J_t.txt" );
        write_matrix( Se, "Se_t.txt" );
        write_matrix( Sa, "Sa_t.txt" );

        Index t, t1, t2, t_m;

        inv( SeInv, Se );
        inv( SxInv, Sa );

        // n-form
        t1 = clock();
        mult( yf, J, xa );
        // oem_linear_nform( x_nform, G_nform, J, yf, cost_x, cost_y, K,
        //                   xa, x_norm, y, SeInv, SxInv, cost_x, false );
        t2 = clock();
        t = (t2 - t1) * 1000 / CLOCKS_PER_SEC;

        // m-form
        mult( yf, J, xa );
        // oem_linear_mform( x_mform, G_mform, xa, yf, y, J, Se, Sa );

        // Matlab
        t_m = run_oem_matlab( x_m, G_m, eng, "test_oem" );

        ofs << setw(5) << n << setw(10) << t << setw(10) << 42; // Dummy column
        ofs << setw(10) << t_m << endl;
        cout << setw(5) << n << setw(10) << t << setw(10) << t_m;
        cout << endl << endl;

        n += step;
    }
    cout << endl << endl;

    // Tidy up
    ofs.close();
    run_plot_script( eng, "times_linear.txt", "Linear OEM" );

}

//! Test linear_oem.
/*!
  Tests the linear_oem function using randomized input data. Performs
  ntest numbers of tests with a m times n K-matrix. For each test, the
  maximum relative error is printed to standard out.

  \param[in] m Size of the measurement space.
  \param[in] n Size of the state space.
  \param[in] ntests Number of tests to be performed.
*/
void test_oem_linear( Engine* eng,
                      Index m,
                      Index n,
                      Index ntests )
{
    Vector x(n), x_n(n), x_g(n), x_m(n), y(n), yf(n), xa(n), zero(n), x_norm(n);
    Matrix J(n,n), Se(n,n), Sa(n,n), SeInv(n,n), SxInv(n,n), G(n,m), G_mform(n,m),
        G_m(n,m);
    LinearModel K;

    zero = 0.0;

    cout << "Testing linear OEM: m = " << m << ", n = ";
    cout << n << ", ntests = " << ntests << endl << endl;

    cout << "Test No. " << setw(15) << "Standard";
    cout << setw(15) << "Normalized" << setw(15) << "Gain Matrix" << endl;

    // Run tests.
    for ( Index i = 0; i < ntests; i++ )
    {
        generate_linear_model( J );
        generate_test_data( y, xa, Se, Sa );

        inv( SeInv, Se );
        inv( SxInv, Sa );
        LinearOEM oem( J, SeInv, xa, SxInv );

        for (Index j = 0; j < n; j++ )
        {
            x_norm[j] = sqrt(Sa(j,j));
        }

        write_vector( xa, "xa_t.txt" );
        write_vector( y, "y_t.txt" );
        write_matrix( J, "J_t.txt" );
        write_matrix( Se, "Se_t.txt" );
        write_matrix( Sa, "Sa_t.txt" );

        // m-form
        mult( yf, J, xa );

        oem.set_x_norm( x_norm );
        oem.compute( x, y, yf );
        oem.compute( x_g, G, y, yf );
        oem.compute( x_n, y, yf );

        run_oem_matlab( x_m, G_m, eng, "test_oem" );

        Numeric err, err_norm, err_g;
        err = get_maximum_error( x, x_m, true );
        err_norm = get_maximum_error( x_n, x_m, true );
        err_g = get_maximum_error( G, G_m, true );

        cout << setw(8) << i+1;
        cout << setw(15) << err << setw(15) << err_norm << setw(15) << err_g;
        cout << endl;

    }
    cout << endl;
}

//! Test non-linear OEM
/*!
  Test the non-linear OEM functions using the Gauss-Newton method using a random
  quadratic model. Performs ntest numbers of test with a state space of
  dimension n and a measurement space of dimension m. Test each iteration method
  and compares the result to the Matlab result. The maximum relative error is
  printed to stdout.

  \param eng The Matlba engine handle.
  \param m  The dimension of the measurement space space.
  \param n  The dimension of the state space.
  \param ntests The number of tests to perform.
*/
void test_oem_gauss_newton( Engine *eng,
                            Index m,
                            Index n,
                            Index ntests )
{
    Vector y0(m), y(m), y_m(m), x(n), x0(n), x_n(x), x_m(n), x_norm(n), xa(n);
    Matrix Se(m,m), Sa(n,n), SeInv(m,m), SxInv(n,n), G(n,m), G_m(n,m), J(n,m);

    cout << "Testing Gauss-Newton OEM: m = " << m << ", n = ";
    cout << n << ", ntests = " << ntests << endl << endl;

    cout << "Test No. " << setw(15) << "Standard" << setw(15) << "Normalized";
    cout << setw(15) << "No. Iterations" << endl;

    // Run tests.
    for ( Index i = 0; i < ntests; i++ )
    {
        QuadraticModel K(m,n);
        generate_test_data( y0, xa, Se, Sa );
        x0 = xa;
        add_noise( x0, 0.01 );
        K.evaluate( y0, x0);

        inv( SeInv, Se );
        inv( SxInv, Sa );

        NonLinearOEM oem( SeInv, xa, SxInv, K, GAUSS_NEWTON );

        for (Index j = 0; j < n; j++ )
        {
            x_norm[j] = sqrt(abs(Sa(j,j)));
        }

        write_vector( xa, "xa_t.txt" );
        write_vector( y0, "y_t.txt" );
        write_matrix( Se, "Se_t.txt" );
        write_matrix( Sa, "Sa_t.txt" );

        oem.compute( x, G, y0, false );
        oem.set_x_norm( x_norm );
        oem.compute( x_n, y0, false );
        run_oem_matlab( x_m, G_m, eng, "test_oem_gauss_newton" );

        cout << setw(9) << i+1 << setw(15);
        cout << get_maximum_error( x, x_m, true ) << setw(15);
        cout << get_maximum_error( x_n, x_m, true ) << setw(15);
        cout << oem.iterations() << setw(15);
        cout << endl;

    }
    cout << endl;
}

//! Test non-linear OEM
/*!
  Test the non-linear OEM function using Levenberg-Marquardt method with a random
  quadratic model. Performs ntest numbers of tests with a state space of
  dimension n and a measurement space of dimension m. Test each iteration method
  and compares the result to the Matlab result. The maximum relative error is
  printed to stdout.

  \param eng The Matlba engine handle.
  \param m  The dimension of the measurement space space.
  \param n  The dimension of the state space.
  \param ntests The number of tests to perform.
*/
void test_oem_levenberg_marquardt( Engine *eng,
                                   Index m,
                                   Index n,
                                   Index ntests )
{
    Vector y0(m), y(m), y_m(m), x(n), x0(n), x_n(x), x_m(n), x_norm(n), xa(n);
    Matrix Se(m,m), Sx(n,n), SeInv(m,m), SxInv(n,n), G(n,m), G_m(n,m), J(n,m);

    cout << "Testing Levenberg-Marquardt OEM: m = " << m << ", n = ";
    cout << n << ", ntests = " << ntests << endl << endl;

    cout << "Test No. " << setw(15) << "Standard" << setw(15) << "Normalized";
    cout << setw(15) << "No. Iterations" << endl;

    // Run tests.
    for ( Index i = 0; i < ntests; i++ )
    {
        QuadraticModel K(m,n);
        generate_test_data( y0, xa, Se, Sx );
        x0 = xa;
        add_noise( x0, 0.01 );
        K.evaluate( y0, x0);

        inv( SeInv, Se );
        inv( SxInv, Sx );

        NonLinearOEM oem( SeInv, xa, SxInv, K, LEVENBERG_MARQUARDT );

        for (Index j = 0; j < n; j++ )
        {
            x_norm[j] = sqrt(abs(Sx(j,j)));
        }

        write_vector( xa, "xa_t.txt" );
        write_vector( y0, "y_t.txt" );
        write_matrix( Se, "Se_t.txt" );
        write_matrix( Sx, "Sx_t.txt" );

        oem.compute( x, y0, false );
        oem.set_x_norm( x_norm );
        oem.compute( x_n, y0, false );
        run_oem_matlab( x_m, G_m, eng, "test_oem_levenberg_marquardt" );

        cout << setw(9) << i+1 << setw(15);
        cout << get_maximum_error( x, x_m, true ) << setw(15);
        cout << get_maximum_error( x_n, x_m, true ) << setw(15);
        cout << oem.iterations() << setw(15);
        cout << endl;

    }
    cout << endl;
}


int main()
{

    //Set up the test environment.
    Engine * eng;
    setup_test_environment( eng );

    // Run tests and benchmarks.
    //test_oem_linear( eng, 50, 50, 10 );
    //test_oem_gauss_newton( eng, 50, 50, 10 );
    test_oem_levenberg_marquardt( eng, 50, 50, 10 );

    //benchmark_inv( eng, 100, 2000, 16);
    //benchmark_mult( eng, 100, 2000, 16);
    //test_oem_linear( eng, 200, 200, 5 );
    //benchmark_oem_linear( eng, 100, 2000, 16);

    // Tidy up test environment.
    tidy_up_test_environment( eng );

}
