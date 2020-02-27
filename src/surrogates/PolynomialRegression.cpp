/*  _______________________________________________________________________

    DAKOTA: Design Analysis Kit for Optimization and Terascale Applications
    Copyright 2014 Sandia Corporation.
    This software is distributed under the GNU Lesser General Public License.
    For more information, see the README file in the top Dakota directory.
    _______________________________________________________________________ */

#include "PolynomialRegression.hpp"

namespace dakota {
namespace surrogates {

// ------------------------------------------------------------

// Constructor

PolynomialRegression::PolynomialRegression(const MatrixXi & indices, int nvars) :
  basis_indices(std::make_shared<MatrixXi>(indices)),
  num_terms(indices.cols()),
  num_vars(nvars) // should have these come in through options once that mechanism exists - RWH
{
  scaler_type = util::SCALER_TYPE::NONE;
}

// ------------------------------------------------------------
// Destructor

PolynomialRegression::~PolynomialRegression() {}

// ------------------------------------------------------------
// Getters

const MatrixXd & PolynomialRegression::get_samples() const { return *samples_; }

const MatrixXd & PolynomialRegression::get_response() const { return *response; }

int PolynomialRegression::get_polynomial_order() const { return polynomial_order; }

util::SCALER_TYPE PolynomialRegression::get_scaler_type() const { return scaler_type; }

const MatrixXd & PolynomialRegression::get_polynomial_coeffs() const { return *polynomial_coeffs; }

double PolynomialRegression::get_polynomial_intercept() const { return polynomial_intercept; }

const util::LinearSolverBase & PolynomialRegression::get_solver() const { return *solver; }

// Setters

void PolynomialRegression::set_samples(const MatrixXd & samples) { samples_ = std::make_shared<MatrixXd>(samples); }

void PolynomialRegression::set_response(const MatrixXd & response_) { response = std::make_shared<MatrixXd>(response_); }

void PolynomialRegression::set_polynomial_order(int polynomial_order_) { polynomial_order = polynomial_order_; }

void PolynomialRegression::set_scaler_type(const util::SCALER_TYPE scaler_type_) { scaler_type = scaler_type_; }

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
  basis_matrix = MatrixXd::Zero(num_samples, num_terms);

  for(int j=0; j<num_terms; ++j) {
    for(int i=0; i<num_samples; ++i) {
      double val = 1.0;
      for( int d=0; d<num_vars; ++d )
	val *= std::pow(samples(i,d), (*basis_indices)(d,j));
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
  scaler = util::scaler_factory(scaler_type, unscaled_basis_matrix);
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
  const int num_eval_points = eval_points.rows();

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

} // namespace surrogates
} // namespace dakota
