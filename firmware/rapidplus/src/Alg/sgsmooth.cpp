// From : https://raw.githubusercontent.com/thatchristoph/vmd-cvs-github/master/plugins/signalproc/src/sgsmooth.C
//!
// Sliding window signal processing (and linear algebra toolkit).
//
// supported operations:
// <ul>
// <li> Savitzky-Golay smoothing.
// <li> computing a numerical derivative based of Savitzky-Golay smoothing.
// <li> required linear algebra support for SG smoothing using STL based
//      vector/matrix classes
// </ul>
//
// \brief Linear Algebra "Toolkit".
//
// modified by Rob Patro, 2016

// system headers
#include <cstdio>
#include <cstddef>             // for size_t
#include <cmath>               // for fabs
#include <vector>

//! default convergence
static const double TINY_FLOAT = 1.0e-300;

//! comfortable array of doubles
using float_vect = std::vector<double>;
//! comfortable array of ints;
using int_vect = std::vector<int>;

/*! matrix class.
 *
 * This is a matrix class derived from a vector of float_vects.  Note that
 * the matrix elements indexed [row][column] with indices starting at 0 (c
 * style). Also note that because of its design looping through rows should
 * be faster than looping through columns.
 *
 * \brief two dimensional floating point array
 */
class float_mat : public std::vector<float_vect> {
private:
    //! disable the default constructor
    explicit float_mat() {};
    //! disable assignment operator until it is implemented.
    float_mat& operator =(const float_mat&) { return *this; };
public:
    //! constructor with sizes
    float_mat(const size_t rows, const size_t cols, const double def = 0.0);
    //! copy constructor for matrix
    float_mat(const float_mat& m);
    //! copy constructor for vector
    float_mat(const float_vect& v);

    //! use default destructor
    // ~float_mat() {};

    //! get size
    size_t nr_rows(void) const { return size(); };
    //! get size
    size_t nr_cols(void) const { return front().size(); };
};



// constructor with sizes
/***********************************************************************
 * Function: float_mat()
 * Description: Sizing constructor for the 2D matrix; allocates 'rows'
 *  float_vect rows and resizes each row to 'cols', filling every element
 *  with 'defval'. Reports an error string if rows or cols is < 1.
 * pramameter: rows = number of matrix rows; cols = number of matrix
 *  columns; defval = default fill value for every element (default 0.0)
 *  return: none (constructor)
 */
float_mat::float_mat(const size_t rows, const size_t cols, const double defval)
    : std::vector<float_vect>(rows) {
    int i;
    for (i = 0; i < rows; ++i) {
        (*this)[i].resize(cols, defval);
    }
    if ((rows < 1) || (cols < 1)) {
        char buffer[1024];

        sprintf(buffer, "cannot build matrix with %d rows and %d columns\n",
            rows, cols);
        //sgs_error(buffer);
    }
}

// copy constructor for matrix
/***********************************************************************
 * Function: float_mat()
 * Description: Copy constructor that deep-copies another float_mat by
 *  iterating each source row, resizing the destination row to match and
 *  assigning the row vector, producing an independent matrix copy.
 * pramameter: m = source matrix to copy from
 *  return: none (constructor)
 */
float_mat::float_mat(const float_mat& m) : std::vector<float_vect>(m.size()) {

    float_mat::iterator inew = begin();
    float_mat::const_iterator iold = m.begin();
    for (/* empty */; iold < m.end(); ++inew, ++iold) {
        const size_t oldsz = iold->size();
        inew->resize(oldsz);
        const float_vect oldvec(*iold);
        *inew = oldvec;
    }
}

// copy constructor for vector
/***********************************************************************
 * Function: float_mat()
 * Description: Constructs a single-row matrix from a 1D vector; allocates
 *  one row, resizes it to the vector length and copies the vector into
 *  that row (treating the vector as a 1xN row matrix).
 * pramameter: v = source vector to wrap as one matrix row
 *  return: none (constructor)
 */
float_mat::float_mat(const float_vect& v)
    : std::vector<float_vect>(1) {

    const size_t oldsz = v.size();
    front().resize(oldsz);
    front() = v;
}

//////////////////////
// Helper functions //
//////////////////////

//! permute() orders the rows of A to match the integers in the index array.
/***********************************************************************
 * Function: permute()
 * Description: Physically reorders the rows of matrix A so they follow the
 *  permutation given in idx; tracks a working index map and swaps rows
 *  in place until each position holds the row indicated by idx.
 * pramameter: A = matrix whose rows are reordered in place; idx = target
 *  permutation of row indices (e.g. from an LU decomposition)
 *  return: none (A is modified in place)
 */
void permute(float_mat& A, int_vect& idx)
{
    int_vect i(idx.size());
    int j, k;

    for (j = 0; j < A.nr_rows(); ++j) {
        i[j] = j;
    }

    // loop over permuted indices
    for (j = 0; j < A.nr_rows(); ++j) {
        if (i[j] != idx[j]) {

            // search only the remaining indices
            for (k = j + 1; k < A.nr_rows(); ++k) {
                if (i[k] == idx[j]) {
                    std::swap(A[j], A[k]); // swap the rows and
                    i[k] = i[j];     // the elements of
                    i[j] = idx[j];   // the ordered index.
                    break; // next j
                }
            }
        }
    }
}

/*! \brief Implicit partial pivoting.
 *
 * The function looks for pivot element only in rows below the current
 * element, A[idx[row]][column], then swaps that row with the current one in
 * the index map. The algorithm is for implicit pivoting (i.e., the pivot is
 * chosen as if the max coefficient in each row is set to 1) based on the
 * scaling information in the vector scale. The map of swapped indices is
 * recorded in swp. The return value is +1 or -1 depending on whether the
 * number of row swaps was even or odd respectively. */
/***********************************************************************
 * Function: partial_pivot()
 * Description: Performs implicit partial pivoting for LU decomposition;
 *  scans rows at and below 'row' for the largest scaled coefficient in
 *  column 'col' (|A|*scale), then swaps that row to the diagonal via the
 *  index map idx, flipping the swap-parity sign on each exchange.
 * pramameter: A = matrix being factorized; row = current pivot row; col =
 *  current pivot column; scale = per-row implicit scaling factors; idx =
 *  index map updated with the row swap; tol = pivot tolerance (replaced
 *  with TINY_FLOAT if <= 0)
 *  return: +1 if no swap performed, -1 if the pivot row was swapped (swap
 *  parity contribution)
 */
static int partial_pivot(float_mat& A, const size_t row, const size_t col,
    float_vect& scale, int_vect& idx, double tol)
{
    if (tol <= 0.0)
        tol = TINY_FLOAT;

    int swapNum = 1;

    // default pivot is the current position, [row,col]
    int pivot = row;
    double piv_elem = fabs(A[idx[row]][col]) * scale[idx[row]];

    // loop over possible pivots below current
    int j;
    for (j = row + 1; j < A.nr_rows(); ++j) {

        const double tmp = fabs(A[idx[j]][col]) * scale[idx[j]];

        // if this elem is larger, then it becomes the pivot
        if (tmp > piv_elem) {
            pivot = j;
            piv_elem = tmp;
        }
    }

#if 0
    if (piv_elem < tol) {
        //sgs_error("partial_pivot(): Zero pivot encountered.\n")
#endif

        if (pivot > row) {           // bring the pivot to the diagonal
            j = idx[row];           // reorder swap array
            idx[row] = idx[pivot];
            idx[pivot] = j;
            swapNum = -swapNum;     // keeping track of odd or even swap
        }
        return swapNum;
    }

    /*! \brief Perform backward substitution.
     *
     * Solves the system of equations A*b=a, ASSUMING that A is upper
     * triangular. If diag==1, then the diagonal elements are additionally
     * assumed to be 1.  Note that the lower triangular elements are never
     * checked, so this function is valid to use after a LU-decomposition in
     * place.  A is not modified, and the solution, b, is returned in a. */
    /***********************************************************************
     * Function: lu_backsubst()
     * Description: Solves A*b=a by back substitution assuming A is upper
     *  triangular; iterates rows from bottom to top, subtracting the
     *  contribution of already-solved columns and (unless diag) dividing
     *  by the diagonal. The solution overwrites 'a'.
     * pramameter: A = upper-triangular matrix (e.g. U from LU); a = right
     *  hand side, overwritten with the solution b; diag = if true treat the
     *  diagonal elements as 1 and skip the division (default false)
     *  return: none (solution returned in a)
     */
    static void lu_backsubst(float_mat & A, float_mat & a, bool diag = false)
    {
        int r, c, k;

        for (r = (A.nr_rows() - 1); r >= 0; --r) {
            for (c = (A.nr_cols() - 1); c > r; --c) {
                for (k = 0; k < A.nr_cols(); ++k) {
                    a[r][k] -= A[r][c] * a[c][k];
                }
            }
            if (!diag) {
                for (k = 0; k < A.nr_cols(); ++k) {
                    a[r][k] /= A[r][r];
                }
            }
        }
    }

    /*! \brief Perform forward substitution.
     *
     * Solves the system of equations A*b=a, ASSUMING that A is lower
     * triangular. If diag==1, then the diagonal elements are additionally
     * assumed to be 1.  Note that the upper triangular elements are never
     * checked, so this function is valid to use after a LU-decomposition in
     * place.  A is not modified, and the solution, b, is returned in a. */
    /***********************************************************************
     * Function: lu_forwsubst()
     * Description: Solves A*b=a by forward substitution assuming A is lower
     *  triangular; iterates rows from top to bottom, subtracting the
     *  contribution of already-solved columns and (unless diag) dividing
     *  by the diagonal. The solution overwrites 'a'.
     * pramameter: A = lower-triangular matrix (e.g. L from LU); a = right
     *  hand side, overwritten with the solution b; diag = if true treat the
     *  diagonal elements as 1 and skip the division (default true)
     *  return: none (solution returned in a)
     */
    static void lu_forwsubst(float_mat & A, float_mat & a, bool diag = true)
    {
        int r, k, c;
        for (r = 0; r < A.nr_rows(); ++r) {
            for (c = 0; c < r; ++c) {
                for (k = 0; k < A.nr_cols(); ++k) {
                    a[r][k] -= A[r][c] * a[c][k];
                }
            }
            if (!diag) {
                for (k = 0; k < A.nr_cols(); ++k) {
                    a[r][k] /= A[r][r];
                }
            }
        }
    }

    /*! \brief Performs LU factorization in place.
     *
     * This is Crout's algorithm (cf., Num. Rec. in C, Section 2.3).  The map of
     * swapped indeces is recorded in idx. The return value is +1 or -1
     * depending on whether the number of row swaps was even or odd
     * respectively.  idx must be preinitialized to a valid set of indices
     * (e.g., {1,2, ... ,A.nr_rows()}). */
    /***********************************************************************
     * Function: lu_factorize()
     * Description: Performs in-place LU factorization of square matrix A
     *  using Crout's algorithm with implicit partial pivoting; first
     *  computes per-row scaling from the max absolute element, then loops
     *  over columns calling partial_pivot and eliminating to build L and U,
     *  finally permuting A into pivoted order. Records swaps in idx.
     * pramameter: A = square matrix factorized in place into L/U; idx =
     *  preinitialized index array updated with the row permutation; tol =
     *  pivot tolerance (replaced with TINY_FLOAT if <= 0)
     *  return: +1/-1 swap parity (even/odd row swaps), or 0 if A is empty,
     *  nonsquare, or a zero pivot is found
     */
    static int lu_factorize(float_mat & A, int_vect & idx, double tol = TINY_FLOAT)
    {
        if (tol <= 0.0)
            tol = TINY_FLOAT;

        if ((A.nr_rows() == 0) || (A.nr_rows() != A.nr_cols())) {
            //sgs_error("lu_factorize(): cannot handle empty "
            //           "or nonsquare matrices.\n");

            return 0;
        }

        float_vect scale(A.nr_rows());  // implicit pivot scaling
        int i, j;
        for (i = 0; i < A.nr_rows(); ++i) {
            double maxval = 0.0;
            for (j = 0; j < A.nr_cols(); ++j) {
                if (fabs(A[i][j]) > maxval)
                    maxval = fabs(A[i][j]);
            }
            if (maxval == 0.0) {
                //sgs_error("lu_factorize(): zero pivot found.\n");
                return 0;
            }
            scale[i] = 1.0 / maxval;
        }

        int swapNum = 1;
        int c, r;
        for (c = 0; c < A.nr_cols(); ++c) {            // loop over columns
            swapNum *= partial_pivot(A, c, c, scale, idx, tol); // bring pivot to diagonal
            for (r = 0; r < A.nr_rows(); ++r) {      //  loop over rows
                int lim = (r < c) ? r : c;
                for (j = 0; j < lim; ++j) {
                    A[idx[r]][c] -= A[idx[r]][j] * A[idx[j]][c];
                }
                if (r > c)
                    A[idx[r]][c] /= A[idx[c]][c];
            }
        }
        permute(A, idx);
        return swapNum;
    }

    /*! \brief Solve a system of linear equations.
     * Solves the inhomogeneous matrix problem with lu-decomposition. Note that
     * inversion may be accomplished by setting a to the identity_matrix. */
    /***********************************************************************
     * Function: lin_solve()
     * Description: Solves the linear system A*X=a via LU decomposition;
     *  copies A and a, LU-factorizes the copy of A, permutes the right hand
     *  side to match, then runs forward and backward substitution to
     *  produce X. Passing the identity for 'a' yields the inverse of A.
     * pramameter: A = coefficient matrix; a = right hand side matrix (use
     *  identity to invert); tol = pivot tolerance for the factorization
     *  return: the solution matrix X (= A^-1 * a)
     */
    static float_mat lin_solve(const float_mat & A, const float_mat & a,
        double tol = TINY_FLOAT)
    {
        float_mat B(A);
        float_mat b(a);
        int_vect idx(B.nr_rows());
        int j;

        for (j = 0; j < B.nr_rows(); ++j) {
            idx[j] = j;  // init row swap label array
        }
        lu_factorize(B, idx, tol); // get the lu-decomp.
        permute(b, idx);          // sort the inhomogeneity to match the lu-decomp
        lu_forwsubst(B, b);       // solve the forward problem
        lu_backsubst(B, b);       // solve the backward problem
        return b;
    }

    ///////////////////////
    // related functions //
    ///////////////////////

    //! Returns the inverse of a matrix using LU-decomposition.
    /***********************************************************************
     * Function: invert()
     * Description: Computes the inverse of square matrix A by building an
     *  identity matrix E of the same size and solving A*X=E with lin_solve
     *  (LU-decomposition), so X is A^-1.
     * pramameter: A = square matrix to invert
     *  return: the inverse matrix A^-1
     */
    static float_mat invert(const float_mat & A)
    {
        const int n = A.size();
        float_mat E(n, n, 0.0);
        float_mat B(A);
        int i;

        for (i = 0; i < n; ++i) {
            E[i][i] = 1.0;
        }

        return lin_solve(B, E);
    }

    //! returns the transposed matrix.
    /***********************************************************************
     * Function: transpose()
     * Description: Returns the transpose of matrix a by allocating a result
     *  of size (cols x rows) and copying res[j][i] = a[i][j] for every
     *  element, swapping rows and columns.
     * pramameter: a = matrix to transpose
     *  return: the transposed matrix (a^T)
     */
    static float_mat transpose(const float_mat & a)
    {
        float_mat res(a.nr_cols(), a.nr_rows());
        int i, j;

        for (i = 0; i < a.nr_rows(); ++i) {
            for (j = 0; j < a.nr_cols(); ++j) {
                res[j][i] = a[i][j];
            }
        }
        return res;
    }

    //! matrix multiplication.
    /***********************************************************************
     * Function: operator*()
     * Description: Standard matrix multiplication; computes res = a*b where
     *  each res[i][j] is the dot product of row i of a with column j of b.
     *  Returns an unfilled result if inner dimensions are incompatible.
     * pramameter: a = left matrix (rows x k); b = right matrix (k x cols)
     *  return: the product matrix a*b (a.nr_rows() x b.nr_cols())
     */
    float_mat operator *(const float_mat & a, const float_mat & b)
    {
        float_mat res(a.nr_rows(), b.nr_cols());
        if (a.nr_cols() != b.nr_rows()) {
            //sgs_error("incompatible matrices in multiplication\n");
            return res;
        }

        int i, j, k;

        for (i = 0; i < a.nr_rows(); ++i) {
            for (j = 0; j < b.nr_cols(); ++j) {
                double sum(0.0);
                for (k = 0; k < a.nr_cols(); ++k) {
                    sum += a[i][k] * b[k][j];
                }
                res[i][j] = sum;
            }
        }
        return res;
    }


    //! calculate savitzky golay coefficients.
    /***********************************************************************
     * Function: sg_coeff()
     * Description: Computes Savitzky-Golay convolution coefficients by a
     *  polynomial least-squares fit; builds the Vandermonde design matrix A
     *  (A[i][j]=i^j), solves the normal equations c=(A^T A)^-1 A^T b, then
     *  evaluates the degree-'deg' polynomial at each index to yield the
     *  smoothing coefficients for the supplied unit vector b.
     * pramameter: b = (unit) response vector defining the window position;
     *  deg = polynomial degree of the fit
     *  return: vector of Savitzky-Golay coefficients (same length as b)
     */
    static float_vect sg_coeff(const float_vect & b, const size_t deg)
    {
        const size_t rows(b.size());
        const size_t cols(deg + 1);
        float_mat A(rows, cols);
        float_vect res(rows);

        // generate input matrix for least squares fit
        int i, j;
        for (i = 0; i < rows; ++i) {
            for (j = 0; j < cols; ++j) {
                A[i][j] = pow(double(i), double(j));
            }
        }

        float_mat c(invert(transpose(A) * A) * (transpose(A) * transpose(b)));

        for (i = 0; i < b.size(); ++i) {
            res[i] = c[0][0];
            for (j = 1; j <= deg; ++j) {
                res[i] += c[j][0] * pow(double(i), double(j));
            }
        }
        return res;
    }

    /*! \brief savitzky golay smoothing.
     *
     * This method means fitting a polynome of degree 'deg' to a sliding window
     * of width 2w+1 throughout the data.  The needed coefficients are
     * generated dynamically by doing a least squares fit on a "symmetric" unit
     * vector of size 2w+1, e.g. for w=2 b=(0,0,1,0,0). evaluating the polynome
     * yields the sg-coefficients.  at the border non symmectric vectors b are
     * used. */
    /***********************************************************************
     * Function: sg_smooth()
     * Description: Applies Savitzky-Golay smoothing to signal v over a
     *  sliding window of size 2*width+1. For deg==0 it does a plain moving
     *  average (with shrinking windows at the borders); otherwise it
     *  generates SG coefficients via sg_coeff for the symmetric interior
     *  and for each non-symmetric border position, then convolves them with
     *  the data to produce the smoothed output.
     * pramameter: v = input data vector; width = half-window size (window =
     *  2*width+1); deg = polynomial degree (0 = moving average)
     *  return: smoothed data vector (same length as v; zero-filled on a
     *  parameter error)
     */
    float_vect sg_smooth(const float_vect & v, const int width, const int deg)
    {
        float_vect res(v.size(), 0.0);
        if ((width < 1) || (deg < 0) || (v.size() < (2 * width + 2))) {
            //sgs_error("sgsmooth: parameter error.\n");
            return res;
        }

        const int window = 2 * width + 1;
        const int endidx = v.size() - 1;

        // do a regular sliding window average
        int i, j;
        if (deg == 0) {
            // handle border cases first because we need different coefficients
#if defined(_OPENMP)
#pragma omp parallel for private(i,j) schedule(static)
#endif
            for (i = 0; i < width; ++i) {
                const double scale = 1.0 / double(i + 1);
                const float_vect c1(width, scale);
                for (j = 0; j <= i; ++j) {
                    res[i] += c1[j] * v[j];
                    res[endidx - i] += c1[j] * v[endidx - j];
                }
            }

            // now loop over rest of data. reusing the "symmetric" coefficients.
            const double scale = 1.0 / double(window);
            const  float_vect c2(window, scale);
#if defined(_OPENMP)
#pragma omp parallel for private(i,j) schedule(static)
#endif
            for (i = 0; i <= (v.size() - window); ++i) {
                for (j = 0; j < window; ++j) {
                    res[i + width] += c2[j] * v[i + j];
                }
            }
            return res;
        }

        // handle border cases first because we need different coefficients
#if defined(_OPENMP)
#pragma omp parallel for private(i,j) schedule(static)
#endif
        for (i = 0; i < width; ++i) {
            float_vect b1(window, 0.0);
            b1[i] = 1.0;

            const float_vect c1(sg_coeff(b1, deg));
            for (j = 0; j < window; ++j) {
                res[i] += c1[j] * v[j];
                res[endidx - i] += c1[j] * v[endidx - j];
            }
        }

        // now loop over rest of data. reusing the "symmetric" coefficients.
        float_vect b2(window, 0.0);
        b2[width] = 1.0;
        const float_vect c2(sg_coeff(b2, deg));

#if defined(_OPENMP)
#pragma omp parallel for private(i,j) schedule(static)
#endif
        for (i = 0; i <= (v.size() - window); ++i) {
            for (j = 0; j < window; ++j) {
                res[i + width] += c2[j] * v[i + j];
            }
        }
        return res;
    }

    /*! least squares fit a polynome of degree 'deg' to data in 'b'.
     *  then calculate the first derivative and return it. */
    /***********************************************************************
     * Function: lsqr_fprime()
     * Description: Least-squares fits a degree-'deg' polynomial to data b
     *  (Vandermonde matrix A[i][j]=i^j, normal equations c=(A^T A)^-1 A^T b)
     *  and evaluates the analytic first derivative of that polynomial at
     *  each index, returning the derivative values.
     * pramameter: b = input data window to fit; deg = polynomial degree
     *  return: vector of first-derivative values of the fitted polynomial
     *  (same length as b)
     */
    static float_vect lsqr_fprime(const float_vect & b, const int deg)
    {
        const int rows(b.size());
        const int cols(deg + 1);
        float_mat A(rows, cols);
        float_vect res(rows);

        // generate input matrix for least squares fit
        int i, j;
        for (i = 0; i < rows; ++i) {
            for (j = 0; j < cols; ++j) {
                A[i][j] = pow(double(i), double(j));
            }
        }

        float_mat c(invert(transpose(A) * A) * (transpose(A) * transpose(b)));

        for (i = 0; i < b.size(); ++i) {
            res[i] = c[1][0];
            for (j = 1; j < deg; ++j) {
                res[i] += c[j + 1][0] * double(j + 1)
                    * pow(double(i), double(j));
            }
        }
        return res;
    }

    /*! \brief savitzky golay smoothed numerical derivative.
     *
     * This method means fitting a polynome of degree 'deg' to a sliding window
     * of width 2w+1 throughout the data.
     *
     * In contrast to the sg_smooth function we do a brute force attempt by
     * always fitting the data to a polynome of degree 'deg' and using the
     * result. */
    /***********************************************************************
     * Function: sg_derivative()
     * Description: Computes a Savitzky-Golay smoothed numerical first
     *  derivative of signal v over a window of 2*width+1. Handles the lower
     *  and upper borders with single polynomial fits (upper fit reversed,
     *  hence negated), then for interior points slides the window, fits a
     *  degree-'deg' polynomial via lsqr_fprime and takes the middle
     *  derivative value; all samples are scaled by the step size h.
     * pramameter: v = input data vector; width = half-window size; deg =
     *  polynomial degree (>=1); h = sample spacing used to scale the
     *  derivative
     *  return: vector of derivative values (same length as v; zero-filled on
     *  a parameter error)
     */
    float_vect sg_derivative(const float_vect & v, const int width,
        const int deg, const double h)
    {
        float_vect res(v.size(), 0.0);
        if ((width < 1) || (deg < 1) || (v.size() < (2 * width + 2))) {
            //sgs_error("sgsderiv: parameter error.\n");
            return res;
        }

        const int window = 2 * width + 1;

        // handle border cases first because we do not repeat the fit
        // lower part
        float_vect b(window, 0.0);
        int i, j;

        for (i = 0; i < window; ++i) {
            b[i] = v[i] / h;
        }
        const float_vect c(lsqr_fprime(b, deg));
        for (j = 0; j <= width; ++j) {
            res[j] = c[j];
        }
        // upper part. direction of fit is reversed
        for (i = 0; i < window; ++i) {
            b[i] = v[v.size() - 1 - i] / h;
        }
        const float_vect d(lsqr_fprime(b, deg));
        for (i = 0; i <= width; ++i) {
            res[v.size() - 1 - i] = -d[i];
        }

        // now loop over rest of data. wasting a lot of least squares calcs
        // since we only use the middle value.
#if defined(_OPENMP)
#pragma omp parallel for private(i,j) schedule(static)
#endif
        for (i = 1; i < (v.size() - window); ++i) {
            for (j = 0; j < window; ++j) {
                b[j] = v[i + j] / h;
            }
            res[i + width] = lsqr_fprime(b, deg)[width];
        }
        return res;
    }

    // Local Variables:
    // mode: c++
    // c-basic-offset: 4
    // fill-column: 76
    // indent-tabs-mode: nil
    // End: