/*  _______________________________________________________________________

    DAKOTA: Design Analysis Kit for Optimization and Terascale Applications
    Copyright 2014 Sandia Corporation.
    This software is distributed under the GNU Lesser General Public License.
    For more information, see the README file in the top Dakota directory.
    _______________________________________________________________________ */

#include "PolynomialRegression.hpp"
#include "SurrogatesTools.hpp"
#include "../util/CommonUtils.hpp"

namespace dakota {
namespace surrogates {

// ------------------------------------------------------------

// Constructor using "options"

PolynomialRegression::PolynomialRegression(std::shared_ptr<Teuchos::ParameterList> options)
{
  numVars        = options->get<int>("Num Vars");
  int max_degree = options->get<int>("Max Degree");
  double p_norm  = options->get     ("P-Norm", 1.0);

  basisIndices   = std::make_shared<MatrixXi>();
  compute_hyperbolic_indices(numVars, max_degree, p_norm, *basisIndices);
  numTerms       = basisIndices->cols();

  scalerType = util::SCALER_TYPE::NONE;
}

PolynomialRegression::
PolynomialRegression(const MatrixXd& samples_in, const MatrixXd& response_in,
		     Teuchos::ParameterList& options)
{
  // BMA: delegation only required due to shared_ptr
  set_samples(samples_in);
  set_response(response_in);

  // what to do about "Num Vars" option when it's ignored?
  numVars = samples_->cols();

  int max_degree = options.get<int>("Max Degree");
  double p_norm  = options.get     ("P-Norm", 1.0);

  basisIndices   = std::make_shared<MatrixXi>();
  compute_hyperbolic_indices(numVars, max_degree, p_norm, *basisIndices);
  numTerms       = basisIndices->cols();

  scalerType = util::DataScaler::scaler_type(options.get<std::string>("scaler_name", "none"));
}


// ------------------------------------------------------------

// Constructor
// BMA: This ctor has the advantage of building basis without having the data
PolynomialRegression::PolynomialRegression(int total_order, int nvars) :
  numVars(nvars)
{
  basisIndices   = std::make_shared<MatrixXi>();
  compute_hyperbolic_indices(numVars, total_order, 1.0, *basisIndices);
  numTerms       = basisIndices->cols();

  scalerType = util::SCALER_TYPE::NONE;
}

// ------------------------------------------------------------
// Destructor

PolynomialRegression::~PolynomialRegression() {}

// ------------------------------------------------------------
// Getters

const MatrixXd & PolynomialRegression::get_samples() const { return *samples_; }

const MatrixXd & PolynomialRegression::get_response() const { return *response; }

int PolynomialRegression::get_polynomial_order() const { return polynomialOrder; }

util::SCALER_TYPE PolynomialRegression::get_scaler_type() const { return scalerType; }

const MatrixXd & PolynomialRegression::get_polynomial_coeffs() const { return *polynomial_coeffs; }

double PolynomialRegression::get_polynomial_intercept() const { return polynomial_intercept; }

const util::LinearSolverBase & PolynomialRegression::get_solver() const { return *solver; }

// Setters

void PolynomialRegression::set_samples(const MatrixXd & samples) { samples_ = std::make_shared<MatrixXd>(samples); }

void PolynomialRegression::set_response(const MatrixXd & response_) { response = std::make_shared<MatrixXd>(response_); }

void PolynomialRegression::set_polynomial_order(int polynomial_order) { polynomialOrder = polynomial_order; }

void PolynomialRegression::set_scaler_type(const util::SCALER_TYPE scaler_type) { scalerType = scaler_type; }

void PolynomialRegression::set_solver(util::SOLVER_TYPE solver_type_) { solver = solver_factory(solver_type_); }

// ------------------------------------------------------------
// Surrogate

void
PolynomialRegression::compute_basis_matrix(const MatrixXd & samples, MatrixXd & basis_matrix) const
{
  if( samples.size() == 0 )
    return;

  const int num_samples = samples.rows();

  // Generate the basis matrix
  basis_matrix = MatrixXd::Zero(num_samples, numTerms);

  for(int j=0; j<numTerms; ++j) {
    for(int i=0; i<num_samples; ++i) {
      double val = 1.0;
      for( int d=0; d<numVars; ++d )
	val *= std::pow(samples(i,d), (*basisIndices)(d,j));
      basis_matrix(i,j) = val;
    }
  }
}

// ------------------------------------------------------------

void
PolynomialRegression::build_surrogate()
{
  const int num_samples = get_samples().size();
  const int num_responses = get_response().size();

  if(num_samples == 0 || num_responses == 0)
    return;

  // Generate the basis matrix
  MatrixXd unscaled_basis_matrix;
  compute_basis_matrix(get_samples(), unscaled_basis_matrix);

  // Scale the basis matrix.
  scaler = util::scaler_factory(scalerType, unscaled_basis_matrix);
  MatrixXd scaled_basis_matrix = scaler->get_scaled_features();

  // Solve the basis matrix.
  polynomial_coeffs = std::make_shared<MatrixXd>(*response);
  solver->solve(scaled_basis_matrix, *response, *polynomial_coeffs);

  // Compute the intercept
  polynomial_intercept = get_response().mean() - (scaled_basis_matrix*(get_polynomial_coeffs())).mean();
}

// ------------------------------------------------------------

void
PolynomialRegression::surrogate_value(const MatrixXd &eval_points, MatrixXd &approx_values)
{
  // Generate the basis matrix for the eval points
  MatrixXd unscaled_basis_matrix;
  compute_basis_matrix(eval_points, unscaled_basis_matrix);

  // Scale sample points.
  MatrixXd scaled_basis_matrix = *(scaler->scale_samples(unscaled_basis_matrix));

  // Find the polynomial regression values.
  approx_values = scaled_basis_matrix * (get_polynomial_coeffs());
  approx_values = (approx_values.array() + polynomial_intercept).matrix();
}

// ------------------------------------------------------------

/*
int find_matching_row ( const MatrixXd &hyperbolic_indices, const VectorXd &decremented_indices )
{
  double difftol = 1.0e-14;
  for ( int i = 0; i < hyperbolic_indices.cols(); i++ )
  {
    VectorXd this_row = hyperbolic_indices.row(i);
    if ( matrix_equals ( this_row, decremented_indices, difftol ))
    {
      return i;
    }
  }
  return -1;
}
*/

// ------------------------------------------------------------

void
PolynomialRegression::gradient ( const MatrixXd &samples, MatrixXd &gradient ) {
  //MatrixXd polynomial_coeffs = get_polynomial_coeffs();
  //const int num_variables = samples.cols();
  //const int p_norm = 1.0;

  /* BasisIndices should already exist */
  //Eigen::MatrixXi basis_indices;
  //compute_hyperbolic_indices( num_variables, polynomialOrder, p_norm, basis_indices );


  /* cast to double for later multiplication by the basis matrix
   * for the evaluation points */
  MatrixXd basis_indices = (*basisIndices).cast<double>();
  /* work with its transpose */
  basis_indices.transposeInPlace();

  /*
  std::cout << "polynomial basis (cast to double):" << std::endl;
  std::cout << basis_indices << std::endl;
  std::cout << "\n";

  std::cout << "polynomial coeffs" << std::endl;
  std::cout << *polynomial_coeffs << std::endl;
  std::cout << "\n";
  */

  MatrixXd deriv_coeffs = MatrixXd::Zero(numTerms, numVars);

  /*
  VectorXi deriv_indices(numTerms);
  VectorXi beta_indices = MatrixXd::Zero(numTerms);
  */
  MatrixXd diff;
  MatrixXd::Index index;

  //gradient.resize(numVars, basis_indices.cols());
  /* return size for gradient is num_samples by num_features */
  //gradient.resize(samples.rows(), numVars);

  // Looks like this makes a copy
  /*
  MatrixXd derivative_matrix = basis_indices.replicate(1,1);
  std::cout << "deriv matrix" << std::endl;
  std::cout << derivative_matrix << std::endl;
  std::cout << "\n";
  */

  for (int i = 0; i < numVars; i++) {
    MatrixXd dec_basis_indices = basis_indices;
    dec_basis_indices.col(i).array() -= 1.0;
    /*
    std::cout << "decremented array" << std::endl;
    std::cout << dec_basis_indices << std::endl;
    std::cout << "\n";
    */
    for (int k = 0; k < numTerms; k++) {
      if (dec_basis_indices(k,i) > -0.5) {
        diff = basis_indices;
        (diff.rowwise() - dec_basis_indices.row(k)).rowwise().squaredNorm().minCoeff(&index);
        //std::cout << "j = " << j << ", min index = " << index << std::endl;
        deriv_coeffs(index,i) = basis_indices(k,i)*(*polynomial_coeffs)(k);
      }
    }
  }
  /*
  std::cout << "\n";
  std::cout << "deriv coeffs" << std::endl;
  std::cout << deriv_coeffs << std::endl;
  std::cout << "\n";
  */

  /* Generate the basis matrix */
  MatrixXd unscaled_eval_pts_basis_matrix, scaled_eval_pts_basis_matrix;
  compute_basis_matrix(samples, unscaled_eval_pts_basis_matrix);

  /* Scale the basis matrix */
  scaler->scale_samples(unscaled_eval_pts_basis_matrix,
                        scaled_eval_pts_basis_matrix);

  /* Compute the gradient */
  gradient = scaled_eval_pts_basis_matrix*deriv_coeffs;
  /*
  std::cout << "gradient" << std::endl;
  std::cout << gradient << std::endl;
  std::cout << "\n";
  */

  //gradient =
  //}

  //for ( int i = 0; i < num_variables; i++ )
  /*
  for ( int i = 0; i < numVars; i++ )
  {
    int num_derivatives = 0;
    //MatrixXi derivative_matrix = basis_indices.replicate(1,1);
    //VectorXi scaling_factors(basis_indices.cols());
    MatrixXd derivative_matrix = basis_indices.replicate(1,1);
    VectorXd scaling_factors(basis_indices.cols());

    for ( int j = 0; j < basis_indices.cols(); j++ )
    {
      int derivative = basis_indices(i, j) - 1;
      if ( derivative > -1 )
      {
        derivative_matrix(i, j) = derivative;
        num_derivatives++;
      }
      else
      {
        for ( int k = 0; k < numVars; k++ )
        {
          derivative_matrix(k,j) = -1;
        }
      }
      scaling_factors(j) = basis_indices(i,j);
    }

    std::cout << "derivative_matrix:" << std::endl;
    std::cout << "for variable " << i << std::endl;
    std::cout << derivative_matrix << std::endl;

    int gradient_index = 0;
    for ( int j = 0; j < basis_indices.cols(); j++ )
    {
      int beta_index = find_matching_row(basis_indices, derivative_matrix.col(j));
      if ( beta_index != -1 )
      {
        double gradient_value = polynomial_coeffs(beta_index, 0) * scaling_factors(beta_index);
        if ( std::abs(gradient_value) < 1.0e-12 )
        {
          gradient(i, gradient_index) = 0.0;
        }
        else
        {
          gradient(i, gradient_index) = gradient_value;
        }
        gradient_index ++;
      }
    }
    for ( int j = gradient_index; j < basis_indices.cols(); j++)
    {
      gradient(i, j) = 0.0;
    }
  }
*/
}
//#if 0
void
PolynomialRegression::hessian(const MatrixXd &sample, MatrixXd &hessian) {

  hessian.resize(numVars, numVars);
  MatrixXd basis_indices = (*basisIndices).cast<double>();
  basis_indices.transposeInPlace();

  //MatrixXd deriv_coeffs = MatrixXd::Zero(numTerms, numVars);
  MatrixXd deriv_coeffs = VectorXd::Zero(numTerms);
  MatrixXd dec_basis_indices, diff;
  MatrixXd::Index index;

  /* Generate the (row) basis matrix */
  MatrixXd unscaled_eval_pts_basis_matrix, scaled_eval_pts_basis_matrix;
  compute_basis_matrix(sample, unscaled_eval_pts_basis_matrix);

  /* Scale the (row) basis matrix */
  scaler->scale_samples(unscaled_eval_pts_basis_matrix,
                        scaled_eval_pts_basis_matrix);


  for (int i = 0; i < numVars; i++) {
    for (int j = i; j < numVars; j++) {
      deriv_coeffs.setZero();
      dec_basis_indices = basis_indices;
      dec_basis_indices.col(i).array() -= 1.0;
      dec_basis_indices.col(j).array() -= 1.0;
      /*
      std::cout << "decremented array" << std::endl;
      std::cout << dec_basis_indices << std::endl;
      std::cout << "\n";
      */
      for (int k = 0; k < numTerms; k++) {
        /* check for any -1 entries in the relevant row
         * of the decremented basis indices array */
        if (!(dec_basis_indices.row(k).any() < -0.5)) {
          diff = basis_indices;
          (diff.rowwise() - dec_basis_indices.row(k)).rowwise().squaredNorm().minCoeff(&index);
          if (i == j) {
            deriv_coeffs(index) = basis_indices(k,i)*(basis_indices(k,i) - 1.0)*
                                  (*polynomial_coeffs)(k);
          }
          else {
            deriv_coeffs(index) = basis_indices(k,i)*basis_indices(k,j)*
                                  (*polynomial_coeffs)(k);
          }
        }
      }
      hessian(i,j) = (scaled_eval_pts_basis_matrix*deriv_coeffs)(0,0);
      if (i != j) {
        hessian(j,i) = hessian(i,j);
      }
    }
  }
  /*
  std::cout << "\n";
  std::cout << "deriv coeffs" << std::endl;
  std::cout << deriv_coeffs << std::endl;
  std::cout << "\n";
  */

}
//#endif

} // namespace surrogates
} // namespace dakota
