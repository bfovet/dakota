/*  _______________________________________________________________________

    DAKOTA: Design Analysis Kit for Optimization and Terascale Applications
    Copyright 2014 Sandia Corporation.
    This software is distributed under the GNU Lesser General Public License.
    For more information, see the README file in the top Dakota directory.
    _______________________________________________________________________ */

//- Class:       ProbabilityTransformModel
//- Description: Implementation code for the ProbabilityTransformModel class
//- Owner:       Brian Adams
//- Checked by:

#include "ProbabilityTransformModel.hpp"
#include "MarginalsCorrDistribution.hpp"

static const char rcsId[]="@(#) $Id$";

namespace Dakota
{

/// initialization of static needed by RecastModel
ProbabilityTransformModel* ProbabilityTransformModel::ptmInstance(NULL);


ProbabilityTransformModel::
ProbabilityTransformModel(const Model& x_model, short u_space_type,
			  bool truncate_bnds, Real bnd) :
  RecastModel(x_model), distParamDerivs(false), truncatedBounds(truncate_bnds),
  boundVal(bnd), mappingInitialized(false)
{
  modelType = "probability_transform";
  modelId   = recast_model_id(root_model_id(), "PROBABILITY_TRANSFORM");

  // initialize invariant portions of probability transform at construct time
  xDist = x_model.multivariate_distribution(); // shared rep
  initialize_transformation(u_space_type);

  Sizet2DArray vars_map, primary_resp_map, secondary_resp_map;
  SizetArray recast_vars_comps_total; // default: no change in cauv total
  // we do not reorder the u-space variable types such that we preserve a
  // 1-to-1 mapping with consistent ordering
  size_t i, num_active_rv = xDist.active_variables().count();
  //size_t num_rv = xDist.random_variables().size(); // *** TO DO: manage active subsets within NatafTransformation
  vars_map.resize(num_active_rv);
  for (i=0; i<num_active_rv; ++i)
    { vars_map[i].resize(1);         vars_map[i][0] = i; }
  primary_resp_map.resize(numFns);
  for (i=0; i<numFns; ++i)
    { primary_resp_map[i].resize(1); primary_resp_map[i][0] = i; }

  // Nonlinear mappings require special ASV logic for transforming Hessians.
  bool nonlinear_vars_map = nonlinear_variables_mapping(xDist, uDist);
  // There is no additional response mapping beyond that required by the
  // nonlinear variables mapping.
  BoolDequeArray nonlinear_resp_map(numFns, BoolDeque(1, false));
  const Response& x_resp = subModel.current_response();
  const SharedVariablesData& svd = subModel.current_variables().shared_data();
  const BitArray& all_relax_di = svd.all_relaxed_discrete_int();
  const BitArray& all_relax_dr = svd.all_relaxed_discrete_real();
  short recast_resp_order = 1; // recast resp order to be same as original resp
  if (!x_resp.function_gradients().empty()) recast_resp_order |= 2;
  if (!x_resp.function_hessians().empty())  recast_resp_order |= 4;

  init_sizes(recast_vars_comps_total, all_relax_di, all_relax_dr, numFns,
	     0, 0, recast_resp_order);
  init_maps(vars_map, nonlinear_vars_map, vars_u_to_x_mapping,
	    set_u_to_x_mapping, primary_resp_map, secondary_resp_map,
	    nonlinear_resp_map, resp_x_to_u_mapping, NULL);
  // publish inverse mappings for use in data imports.  Since derivatives are
  // not imported and response values are not transformed, an inverse variables
  // transformation is sufficient for this purpose.
  inverse_mappings(vars_x_to_u_mapping, NULL, NULL, NULL);
}


ProbabilityTransformModel::~ProbabilityTransformModel()
{ }


bool ProbabilityTransformModel::initialize_mapping(ParLevLIter pl_iter)
{
  RecastModel::initialize_mapping(pl_iter);
  bool sub_model_resize = subModel.initialize_mapping(pl_iter);

  update_transformation();

  // update message lengths for send/receive of parallel jobs (normally
  // performed once in Model::init_communicators() just after construct time)
  if (sub_model_resize)
    estimate_message_lengths();

  mappingInitialized = true;
  return sub_model_resize;
}


bool ProbabilityTransformModel::finalize_mapping()
{
  mappingInitialized = false;

  bool sub_model_resize = subModel.finalize_mapping();
  RecastModel::finalize_mapping();

  return sub_model_resize; // This will become true when TODO is implemented.
}


void ProbabilityTransformModel::initialize_dakota_variable_types()
{
  // Note: ctor has called initialize_distribution_
  // {transformation,types,correlations}().  Defining the transformation is
  // deferred until Model::initialize_mapping() to allow for problem resizing.

  const SharedVariablesData& svd = subModel.current_variables().shared_data();
  size_t i, num_cdv, num_ddiv, num_ddsv, num_ddrv, num_cauv, num_dauiv,
    num_dausv, num_daurv, num_ceuv, num_deuiv, num_deusv, num_deurv,
    num_csv, num_dsiv,  num_dssv,  num_dsrv, rv_cntr = 0, cv_cntr = 0,
    div_cntr = 0, dsv_cntr = 0, drv_cntr = 0;
  svd.design_counts(num_cdv, num_ddiv, num_ddsv, num_ddrv);
  svd.aleatory_uncertain_counts(num_cauv, num_dauiv, num_dausv, num_daurv);
  svd.epistemic_uncertain_counts(num_ceuv, num_deuiv, num_deusv, num_deurv);
  svd.state_counts(num_csv, num_dsiv, num_dssv, num_dsrv);
  const Pecos::ShortArray& u_types = uDist.random_variable_types();

  // Update active continuous/discrete variable types (needed for Model::
  // continuous_{probability_density,distribution_bounds,distribution_moment}())
  bool cdv, ddv, cauv, dauv, ceuv, deuv, csv, dsv;
  active_var_subsets(cdv, ddv, cauv, dauv, ceuv, deuv, csv, dsv);
  if (cdv)
    for (i=0; i<num_cdv; ++i, ++rv_cntr, ++cv_cntr)
      continuous_variable_type(
	pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), cv_cntr);
  else
    rv_cntr += num_cdv;
  if (ddv) {
    for (i=0; i<num_ddiv; ++i, ++rv_cntr, ++div_cntr)
      discrete_int_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), div_cntr);
    for (i=0; i<num_ddsv; ++i, ++rv_cntr, ++dsv_cntr)
      discrete_string_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), dsv_cntr);
    for (i=0; i<num_ddrv; ++i, ++rv_cntr, ++drv_cntr)
      discrete_real_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), drv_cntr);
  }
  else
    rv_cntr += num_ddiv + num_ddsv + num_ddrv;
  if (cauv)
    for (i=0; i<num_cauv; ++i, ++rv_cntr, ++cv_cntr)
      continuous_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), cv_cntr);
  else
    rv_cntr += num_cauv;
  if (dauv) {
    for (i=0; i<num_dauiv; ++i, ++rv_cntr, ++div_cntr)
      discrete_int_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), div_cntr);
    for (i=0; i<num_dausv; ++i, ++rv_cntr, ++dsv_cntr)
      discrete_string_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), dsv_cntr);
    for (i=0; i<num_daurv; ++i, ++rv_cntr, ++drv_cntr)
      discrete_real_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), drv_cntr);
  }
  else
    rv_cntr += num_dauiv + num_dausv + num_daurv;
  if (ceuv)
    for (i=0; i<num_ceuv; ++i, ++rv_cntr, ++cv_cntr)
      continuous_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), cv_cntr);
  else
    rv_cntr += num_ceuv;
  if (deuv) {
    for (i=0; i<num_deuiv; ++i, ++rv_cntr, ++div_cntr)
      discrete_int_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), div_cntr);
    for (i=0; i<num_deusv; ++i, ++rv_cntr, ++dsv_cntr)
      discrete_string_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), dsv_cntr);
    for (i=0; i<num_deurv; ++i, ++rv_cntr, ++drv_cntr)
      discrete_real_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), drv_cntr);
  }
  else
    rv_cntr += num_deuiv + num_deusv + num_deurv;
  if (csv)
    for (i=0; i<num_csv; ++i, ++rv_cntr, ++cv_cntr)
      continuous_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), cv_cntr);
  else
    rv_cntr += num_csv;
  if (dsv) {
    for (i=0; i<num_dsiv; ++i, ++rv_cntr, ++div_cntr)
      discrete_int_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), div_cntr);
    for (i=0; i<num_dssv; ++i, ++rv_cntr, ++dsv_cntr)
      discrete_string_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), dsv_cntr);
    for (i=0; i<num_dsrv; ++i, ++rv_cntr, ++drv_cntr)
      discrete_real_variable_type(
        pecos_to_dakota_variable_type(u_types[rv_cntr], rv_cntr), drv_cntr);
  }
  else
    rv_cntr += num_dsiv + num_dssv + num_dsrv;
}


void ProbabilityTransformModel::
update_model_bounds(bool truncate_bnds, Real bnd)
{
  // Here, we finesse the continuous "global" bounds for a Model (distinct from
  // distribution bounds in Pecos::MultivariateDistribution), for benefit of
  // methods that are not distribution-aware, instead operating on bounded
  // domains (e.g., PStudyDACE).  While the same concept could be extended to
  // discrete distributions with semi-infinite support, we leave that exercise
  // to be motivated by future use cases.

  ////////////////////////////////////////////////////////////
  // Populate continuous model bounds for transformed space //
  ////////////////////////////////////////////////////////////

  const Pecos::ShortArray& u_types = uDist.random_variable_types();
  const std::vector<Pecos::RandomVariable>& x_rv = xDist.random_variables();
  size_t num_cv = currentVariables.cv(), num_rv = u_types.size();
  // [-1,1] are u-space bounds for design, state, epistemic, uniform, & beta
  RealVector c_l_bnds(num_cv, false);  c_l_bnds = -1.;
  RealVector c_u_bnds(num_cv, false);  c_u_bnds =  1.;
  Real dbl_inf = std::numeric_limits<Real>::infinity();

  // active subset flags
  bool cdv, ddv, cauv, dauv, ceuv, deuv, csv, dsv;
  active_var_subsets(cdv, ddv, cauv, dauv, ceuv, deuv, csv, dsv);
  // raw counts
  const SharedVariablesData& x_svd = subModel.current_variables().shared_data();
  size_t i, num_cdv, num_ddiv, num_ddsv, num_ddrv, num_cauv, num_dauiv,
    num_dausv, num_daurv, num_ceuv, num_deuiv, num_deusv, num_deurv,
    num_csv, num_dsiv,  num_dssv,  num_dsrv, cv_cntr = 0, rv_cntr = 0;
  x_svd.design_counts(num_cdv, num_ddiv, num_ddsv, num_ddrv);
  x_svd.aleatory_uncertain_counts(num_cauv, num_dauiv, num_dausv, num_daurv);
  x_svd.epistemic_uncertain_counts(num_ceuv, num_deuiv, num_deusv, num_deurv);
  x_svd.state_counts(num_csv, num_dsiv, num_dssv, num_dsrv);

  if (truncate_bnds) {
    // truncate unbounded distributions for approaches requiring bounds:
    // > standard sampling modes: model bounds only used for design/state
    // > *_UNIFORM modes: model bounds are used for all active variables

    // all cdv are mapped to [-1,1]
    if (cdv) cv_cntr += num_cdv;
    rv_cntr += num_cdv + num_ddiv + num_ddsv + num_ddrv;

    if (cauv) {
      for (i=0; i<num_cauv; ++i, ++cv_cntr, ++rv_cntr) {
	const Pecos::RandomVariable& rv_i = x_rv[rv_cntr];
	switch (u_types[rv_cntr]) {
	case Pecos::STD_NORMAL:      // mean +/- bnd std devs
	  c_l_bnds[cv_cntr] = -bnd;  c_u_bnds[cv_cntr] =    bnd;  break;
	case Pecos::STD_EXPONENTIAL: // [0, mean + bnd std devs] for beta=1
	  c_l_bnds[cv_cntr] = 0.;    c_u_bnds[cv_cntr] = 1.+bnd;  break;
	case Pecos::STD_GAMMA: {
	  Real mean, stdev;
	  Pecos::GammaRandomVariable::moments_from_params(
	    rv_i.pull_parameter<Real>(Pecos::GA_ALPHA), 1., mean, stdev);
	  c_l_bnds[cv_cntr] = 0.;
	  c_u_bnds[cv_cntr] = mean + bnd * stdev;  break;
	}
	case Pecos::BOUNDED_NORMAL: {
	  // Note: as for NIDR initialization, we use mean,std_dev parameters
	  // rather than computing actual mean,std_dev of bounded distribution
	  Real l_bnd = rv_i.pull_parameter<Real>(Pecos::N_LWR_BND),
	       u_bnd = rv_i.pull_parameter<Real>(Pecos::N_UPR_BND);
	  c_l_bnds[cv_cntr] = (l_bnd > -dbl_inf) ? l_bnd : // use spec bound
	    rv_i.pull_parameter<Real>(Pecos::N_MEAN) - bnd *  // infer bound
	    rv_i.pull_parameter<Real>(Pecos::N_STD_DEV);
	  c_u_bnds[cv_cntr] = (u_bnd <  dbl_inf) ? u_bnd : // use spec bound
	    rv_i.pull_parameter<Real>(Pecos::N_MEAN) + bnd *  // infer bound
	    rv_i.pull_parameter<Real>(Pecos::N_STD_DEV);
	  break;
	}
	case Pecos::LOGNORMAL: // semi-bounded distribution
	  c_l_bnds[cv_cntr] = 0.;
	  c_u_bnds[cv_cntr] = rv_i.pull_parameter<Real>(Pecos::LN_MEAN)
	              + bnd * rv_i.pull_parameter<Real>(Pecos::LN_STD_DEV);
	  break;
	case Pecos::BOUNDED_LOGNORMAL: {
	  c_l_bnds[cv_cntr]
	    = rv_i.pull_parameter<Real>(Pecos::LN_LWR_BND); // spec or 0
	  Real u_bnd  = rv_i.pull_parameter<Real>(Pecos::LN_UPR_BND);
	  if (u_bnd < dbl_inf)
	    c_u_bnds[cv_cntr] = u_bnd; // use spec bound
	  else                         // infer bound
	    // Note: as for NIDR initialization, we use mean,std_dev parameters
	    // rather than computing actual mean,std_dev of bounded distribution
	    c_u_bnds[cv_cntr] = rv_i.pull_parameter<Real>(Pecos::LN_MEAN)
	                + bnd * rv_i.pull_parameter<Real>(Pecos::LN_STD_DEV);
	  break;
	}
	case Pecos::LOGUNIFORM: case Pecos::TRIANGULAR:
	case Pecos::HISTOGRAM_BIN:
	  // bounded distributions: x-space has desired bounds
	  c_l_bnds[cv_cntr] = subModel.continuous_lower_bound(cv_cntr);
	  c_u_bnds[cv_cntr] = subModel.continuous_upper_bound(cv_cntr);
	  break;
	// Note: Could use subModel bounds for the following cases as well
	// except NIDR uses +/-3 sigma, whereas here we're using +/-10 sigma
	case Pecos::GUMBEL: { // unbounded distribution
	  Real mean, stdev;
	  Pecos::GumbelRandomVariable::moments_from_params(
	    rv_i.pull_parameter<Real>(Pecos::GU_ALPHA),
	    rv_i.pull_parameter<Real>(Pecos::GU_BETA), mean, stdev);
	  c_l_bnds[cv_cntr] = mean - bnd * stdev;
	  c_u_bnds[cv_cntr] = mean + bnd * stdev;
	  break;
	}
	case Pecos::FRECHET: { // semibounded distribution
	  Real mean, stdev;
	  Pecos::FrechetRandomVariable::moments_from_params(
	    rv_i.pull_parameter<Real>(Pecos::F_ALPHA),
	    rv_i.pull_parameter<Real>(Pecos::F_BETA), mean, stdev);
	  c_l_bnds[cv_cntr] = 0.; c_u_bnds[cv_cntr] = mean + bnd * stdev; break;
	}
	case Pecos::WEIBULL: { // semibounded distribution
	  Real mean, stdev;
	  Pecos::WeibullRandomVariable::moments_from_params(
	    rv_i.pull_parameter<Real>(Pecos::W_ALPHA),
	    rv_i.pull_parameter<Real>(Pecos::W_BETA), mean, stdev);
	  c_l_bnds[cv_cntr] = 0.; c_u_bnds[cv_cntr] = mean + bnd * stdev; break;
	}
	}
      }
    }
    else
      rv_cntr += num_cauv;
    rv_cntr += num_dauiv + num_dausv + num_daurv;

    if (ceuv) {
      for (i=0; i<num_ceuv; ++i, ++cv_cntr, ++rv_cntr) {
	const Pecos::RandomVariable& rv_i = x_rv[rv_cntr];
	switch (u_types[rv_cntr]) {
	case Pecos::CONTINUOUS_INTERVAL_UNCERTAIN:
	  // bounded distributions: x-space has desired bounds
	  c_l_bnds[cv_cntr] = subModel.continuous_lower_bound(cv_cntr);
	  c_u_bnds[cv_cntr] = subModel.continuous_upper_bound(cv_cntr);
	  break;
	}
      }
    }
    //else
    //  rv_cntr += num_ceuv;
    //rv_cntr += num_deuiv + num_deusv + num_deurv;

    // all csv are mapped to [-1,1]
    //if (csv) cv_cntr += num_csv;
    //rv_cntr += num_csv + num_dsiv + num_dssv + num_dsrv;
  }
  else { // retain infinite model bounds where distributions are unbounded

    // all cdv are mapped to [-1,1]
    if (cdv) cv_cntr += num_cdv;
    rv_cntr += num_cdv + num_ddiv + num_ddsv + num_ddrv;

    if (cauv) {
      for (i=0; i<num_cauv; ++i, ++cv_cntr, ++rv_cntr) {
	const Pecos::RandomVariable& rv_i = x_rv[rv_cntr];
	switch (u_types[rv_cntr]) {
	case Pecos::STD_NORMAL:  case Pecos::GUMBEL: // unbounded distributions
	  c_l_bnds[cv_cntr] = -dbl_inf;  c_u_bnds[cv_cntr] = dbl_inf;   break;
	case Pecos::LOGNORMAL:  case Pecos::STD_EXPONENTIAL:
	case Pecos::STD_GAMMA:  case Pecos::FRECHET:
	case Pecos::WEIBULL:                       // semibounded distributions
	  c_l_bnds[cv_cntr] = 0.;        c_u_bnds[cv_cntr] = dbl_inf;   break;
	case Pecos::BOUNDED_NORMAL:
	  // can't rely on subModel bounds since could be 1-sided
	  c_l_bnds[cv_cntr] = rv_i.pull_parameter<Real>(Pecos::N_LWR_BND);
	  c_u_bnds[cv_cntr] = rv_i.pull_parameter<Real>(Pecos::N_UPR_BND);
	  break;
	case Pecos::BOUNDED_LOGNORMAL:
	  // can't rely on subModel bounds since could be 1-sided
	  c_l_bnds[cv_cntr] = rv_i.pull_parameter<Real>(Pecos::LN_LWR_BND);
	  c_u_bnds[cv_cntr] = rv_i.pull_parameter<Real>(Pecos::LN_UPR_BND);
	  break;
	case Pecos::LOGUNIFORM:  case Pecos::TRIANGULAR:
	case Pecos::HISTOGRAM_BIN:                     // bounded distributions
	  // 2-sided: can rely on subModel bounds
	  c_l_bnds[cv_cntr] = subModel.continuous_lower_bound(cv_cntr);
	  c_u_bnds[cv_cntr] = subModel.continuous_upper_bound(cv_cntr); break;
	}
      }
    }
    else
      rv_cntr += num_cauv;
    rv_cntr += num_dauiv + num_dausv + num_daurv;

    if (ceuv) {
      for (i=0; i<num_ceuv; ++i, ++cv_cntr, ++rv_cntr) {
	const Pecos::RandomVariable& rv_i = x_rv[rv_cntr];
	switch (u_types[rv_cntr]) {
	case Pecos::CONTINUOUS_INTERVAL_UNCERTAIN:
	  // bounded distributions: x-space has desired bounds
	  c_l_bnds[cv_cntr] = subModel.continuous_lower_bound(cv_cntr);
	  c_u_bnds[cv_cntr] = subModel.continuous_upper_bound(cv_cntr);
	  break;
	}
      }
    }
    //else
    //  rv_cntr += num_ceuv;
    //rv_cntr += num_deuiv + num_deusv + num_deurv;

    // all csv are mapped to [-1,1]
    //if (csv) cv_cntr += num_csv;
    //rv_cntr += num_csv + num_dsiv + num_dssv + num_dsrv;
  }

  continuous_lower_bounds(c_l_bnds);  continuous_upper_bounds(c_u_bnds);
}


/** Build ProbabilityTransformation::ranVar arrays containing the
    uncertain variable distribution types and their corresponding
    means/standard deviations.  This function is used when the Model
    variables are in x-space. */
void ProbabilityTransformModel::
initialize_distribution_types(short u_space_type)
{
  // u_space_type is an enumeration for type of u-space transformation:
  // > if STD_NORMAL_U (reliability, AIS, and Wiener PCE/SC), then u-space is
  //   defined exclusively with std normals;
  // > if STD_UNIFORM_U (SC with local & global Hermite basis polynomials),
  //   then u-space is defined exclusively with std uniforms;
  // > if PARTIAL_ASKEY_U (C3 with orthog polynomials), then u-space is defined
  //   by std normals and std uniforms;
  // > if ASKEY_U (PCE/SC using Askey polynomials), then u-space is defined by
  //   std normals, std uniforms, std exponentials, std betas, and std gammas;
  // > if EXTENDED_U (PCE/SC with Askey plus numerically-generated polynomials),
  //   then u-space involves at most linear scaling to std distributions.

  const Pecos::ShortArray& x_types = xDist.random_variable_types();
  size_t i, num_rv = x_types.size();
  Pecos::ShortArray u_types(num_rv, Pecos::NO_TYPE);
  bool err_flag = false;
  switch (u_space_type) {
  case STD_NORMAL_U:  case STD_UNIFORM_U:
    for (i=0; i<num_rv; ++i)
      switch (x_types[i]) {
      case Pecos::DISCRETE_RANGE:      case Pecos::DISCRETE_SET_INT:
      case Pecos::DISCRETE_SET_STRING: case Pecos::DISCRETE_SET_REAL:
      case Pecos::POISSON:             case Pecos::BINOMIAL:
      case Pecos::NEGATIVE_BINOMIAL:   case Pecos::GEOMETRIC:
      case Pecos::HYPERGEOMETRIC:      case Pecos::HISTOGRAM_PT_INT:
      case Pecos::HISTOGRAM_PT_STRING: case Pecos::HISTOGRAM_PT_REAL:
      case Pecos::DISCRETE_INTERVAL_UNCERTAIN:
      case Pecos::DISCRETE_UNCERTAIN_SET_INT:
      case Pecos::DISCRETE_UNCERTAIN_SET_STRING:
      case Pecos::DISCRETE_UNCERTAIN_SET_REAL:
	err_flag = true;                                            break;
      case Pecos::CONTINUOUS_RANGE: case Pecos::CONTINUOUS_INTERVAL_UNCERTAIN:
	u_types[i] = Pecos::STD_UNIFORM;                            break;
      default:
	u_types[i] = (u_space_type == STD_UNIFORM_U)
	           ? Pecos::STD_UNIFORM : Pecos::STD_NORMAL;        break;
      }
    break;
  case PARTIAL_ASKEY_U: // used for cases with limited distrib support (C3)
    for (i=0; i<num_rv; ++i)
      switch (x_types[i]) {
      case Pecos::NORMAL:           case Pecos::BOUNDED_NORMAL:
      case Pecos::LOGNORMAL:        case Pecos::BOUNDED_LOGNORMAL:
      case Pecos::EXPONENTIAL:      case Pecos::GAMMA:
      case Pecos::GUMBEL:           case Pecos::FRECHET:
      case Pecos::WEIBULL: // unbounded or semi-bounded dist; bounded N/logN
	u_types[i] = Pecos::STD_NORMAL;                             break;
      case Pecos::UNIFORM:          case Pecos::LOGUNIFORM:
      case Pecos::TRIANGULAR:       case Pecos::BETA:
      case Pecos::HISTOGRAM_BIN:    case Pecos::CONTINUOUS_RANGE:
      case Pecos::CONTINUOUS_INTERVAL_UNCERTAIN: // bounded
	u_types[i] = Pecos::STD_UNIFORM;                            break;
      // TO DO: discrete types
      default:	               err_flag = true;                     break;
      }
    break;
  case ASKEY_U:
    for (i=0; i<num_rv; ++i)
      switch (x_types[i]) {
      case Pecos::NORMAL:           case Pecos::BOUNDED_NORMAL:
      case Pecos::LOGNORMAL:        case Pecos::BOUNDED_LOGNORMAL:
      case Pecos::GUMBEL:           case Pecos::FRECHET:
      case Pecos::WEIBULL:
	u_types[i] = Pecos::STD_NORMAL;      break;
      case Pecos::UNIFORM:          case Pecos::LOGUNIFORM:
      case Pecos::TRIANGULAR:       case Pecos::HISTOGRAM_BIN:
      case Pecos::CONTINUOUS_RANGE: case Pecos::CONTINUOUS_INTERVAL_UNCERTAIN:
	u_types[i] = Pecos::STD_UNIFORM;     break;
      case Pecos::EXPONENTIAL: u_types[i] = Pecos::STD_EXPONENTIAL; break;
      case Pecos::BETA:        u_types[i] = Pecos::STD_BETA;        break;
      case Pecos::GAMMA:       u_types[i] = Pecos::STD_GAMMA;       break;
      // TO DO: discrete types
      //case Pecos::POISSON:           case Pecos::BINOMIAL:
      //case Pecos::NEGATIVE_BINOMIAL: case Pecos::GEOMETRIC:
      //case Pecos::HYPERGEOMETRIC:
      default:                 err_flag = true;                     break;
      }
    break;
  case EXTENDED_U:
    for (i=0; i<num_rv; ++i)
      switch (x_types[i]) {
      case Pecos::CONTINUOUS_RANGE:  case Pecos::UNIFORM:
	u_types[i] = Pecos::STD_UNIFORM;                            break;
      case Pecos::NORMAL:      u_types[i] = Pecos::STD_NORMAL;      break;
      case Pecos::EXPONENTIAL: u_types[i] = Pecos::STD_EXPONENTIAL; break;
      case Pecos::BETA:        u_types[i] = Pecos::STD_BETA;        break;
      case Pecos::GAMMA:       u_types[i] = Pecos::STD_GAMMA;       break;
      default:                 u_types[i] = x_types[i];             break;
      }
    break;
  }

  if (err_flag) {
    Cerr << "Error: unsupported mapping in ProbabilityTransformModel::"
         << "initialize_distribution_transformation()." << std::endl;
    abort_handler(MODEL_ERROR);
  }

  Pecos::MarginalsCorrDistribution* u_dist_rep
    = (Pecos::MarginalsCorrDistribution*)uDist.multivar_dist_rep();
  u_dist_rep->initialize_types(u_types, xDist.active_variables());
}


void ProbabilityTransformModel::verify_correlation_support(short u_space_type)
{
  if (xDist.correlation()) {
    const Pecos::ShortArray&   x_types = xDist.random_variable_types();
    const Pecos::ShortArray&   u_types = uDist.random_variable_types();
    const Pecos::RealSymMatrix& x_corr = xDist.correlation_matrix();
    const Pecos::BitArray& active_corr = xDist.active_correlations();
    size_t i, j, corr_i, corr_j, num_rv = x_types.size();
    bool no_mask = active_corr.empty();

    // We can only decorrelate in std normal space; therefore, if a variable
    // with a u_type other than STD_NORMAL is correlated with anything, change
    // its u_type to STD_NORMAL.
    if (u_space_type != STD_NORMAL_U) {
      for (i=0, corr_i=0; i<num_rv; ++i)
	if (no_mask || active_corr[i]) {
	  if (u_types[i] != Pecos::STD_NORMAL)
	    // since we don't check all rows, check all columns despite symmetry
	    for (j=0, corr_j=0; j<num_rv; ++j)
	      if (no_mask || active_corr[j]) {
		if (i != j && std::fabs(x_corr(corr_i, corr_j)) >
		    Pecos::SMALL_NUMBER) {
		  Cerr << "\nWarning: u-space type for random variable " << i+1
		       << " changed to\n         STD_NORMAL due to "
		       << "decorrelation requirements.\n";
		  uDist.random_variable_type(Pecos::STD_NORMAL, i);
		  break; // out of inner loop
		}
		++corr_j;
	      }
	  ++corr_i;
	}
    }

    // Check for correlations among variable types (bounded normal, bounded
    // lognormal, loguniform, triangular, beta, and histogram) that are not
    // supported by Der Kiureghian & Liu for correlation warping estimation
    // when transforming to std normals.
    bool err_flag = false;
    for (i=0, corr_i=0; i<num_rv; ++i) {
      bool distribution_error = false;
      if (no_mask || active_corr[i]) {
	short x_type = x_types[i];
	if (x_type == Pecos::BOUNDED_NORMAL    || x_type == Pecos::LOGUNIFORM ||
	    x_type == Pecos::BOUNDED_LOGNORMAL || x_type == Pecos::TRIANGULAR ||
	    x_type == Pecos::BETA || x_type == Pecos::HISTOGRAM_BIN)
	  // since we don't check all rows, check *all* columns despite symmetry
	  for (j=0, corr_j=0; j<num_rv; ++j)
	    if (no_mask || active_corr[j]) {
	      if (i != j && std::fabs(x_corr(corr_i, corr_j)) >
		  Pecos::SMALL_NUMBER)
		{ distribution_error = true; break; }
	      ++corr_j;
	    }
	++corr_i;
      }
      if (distribution_error) {
	Cerr << "Error: correlation warping for Nataf variable transformation "
	     << "of bounded normal,\n       bounded lognormal, loguniform, "
	     << "triangular, beta, and histogram bin\n       distributions is "
	     << "not currently supported.  Error detected for variable " << i+1
	     << "." << std::endl;
	err_flag = true;
      }
    }
    if (err_flag)
      abort_handler(MODEL_ERROR);
  }
}


unsigned short ProbabilityTransformModel::
pecos_to_dakota_variable_type(unsigned short pecos_var_type, size_t rv_index)
{
  const SizetArray& vc_totals
    = subModel.current_variables().shared_data().components_totals();
  switch (pecos_var_type) {
  case Pecos::CONTINUOUS_RANGE:    // non-unique mapping
    return (rv_index < vc_totals[TOTAL_CDV]) ? // not subject to active subsets
      CONTINUOUS_DESIGN : CONTINUOUS_STATE;                    break;
  case Pecos::DISCRETE_RANGE:      // non-unique mapping
    return (rv_index < vc_totals[TOTAL_CDV]  + vc_totals[TOTAL_DDIV]) ?
      DISCRETE_DESIGN_RANGE : DISCRETE_STATE_RANGE;            break;
  case Pecos::DISCRETE_SET_INT:    // non-unique mapping
    return (rv_index < vc_totals[TOTAL_CDV]  + vc_totals[TOTAL_DDIV]) ?
      DISCRETE_DESIGN_SET_INT : DISCRETE_STATE_SET_INT;        break;
  case Pecos::DISCRETE_SET_STRING: // non-unique mapping
    return (rv_index < vc_totals[TOTAL_CDV]  + vc_totals[TOTAL_DDIV] +
	               vc_totals[TOTAL_DDSV]) ?
      DISCRETE_DESIGN_SET_STRING : DISCRETE_STATE_SET_STRING;  break;
  case Pecos::DISCRETE_SET_REAL:   // non-unique mapping
    return (rv_index < vc_totals[TOTAL_CDV]  + vc_totals[TOTAL_DDIV] +
	               vc_totals[TOTAL_DDSV] + vc_totals[TOTAL_DDRV]) ?
      DISCRETE_DESIGN_SET_REAL : DISCRETE_STATE_SET_REAL;      break;
  // continuous aleatory
  case Pecos::STD_NORMAL: case Pecos::NORMAL: case Pecos::BOUNDED_NORMAL:
    return NORMAL_UNCERTAIN;  break;
  case Pecos::LOGNORMAL: case Pecos::BOUNDED_LOGNORMAL:
    return LOGNORMAL_UNCERTAIN; break;
  case Pecos::STD_UNIFORM: case Pecos::UNIFORM:
    return UNIFORM_UNCERTAIN; break;
  case Pecos::LOGUNIFORM:
    return LOGUNIFORM_UNCERTAIN; break;
  case Pecos::TRIANGULAR:
    return TRIANGULAR_UNCERTAIN; break;
  case Pecos::STD_EXPONENTIAL: case Pecos::EXPONENTIAL:
    return EXPONENTIAL_UNCERTAIN; break;
  case Pecos::STD_BETA: case Pecos::BETA:
    return BETA_UNCERTAIN; break;
  case Pecos::STD_GAMMA: case Pecos::GAMMA:
    return GAMMA_UNCERTAIN; break;
  case Pecos::GUMBEL:
    return GUMBEL_UNCERTAIN; break;
  case Pecos::FRECHET:
    return FRECHET_UNCERTAIN; break;
  case Pecos::WEIBULL:
    return WEIBULL_UNCERTAIN; break;
  case Pecos::HISTOGRAM_BIN:
    return HISTOGRAM_BIN_UNCERTAIN; break;
  // discrete aleatory
  case Pecos::POISSON:
    return POISSON_UNCERTAIN; break;
  case Pecos::BINOMIAL:
    return BINOMIAL_UNCERTAIN; break;
  case Pecos::NEGATIVE_BINOMIAL:
    return NEGATIVE_BINOMIAL_UNCERTAIN; break;
  case Pecos::GEOMETRIC:
    return GEOMETRIC_UNCERTAIN; break;
  case Pecos::HYPERGEOMETRIC:
    return HYPERGEOMETRIC_UNCERTAIN; break;
  case Pecos::HISTOGRAM_PT_INT:
    return HISTOGRAM_POINT_UNCERTAIN_INT; break;
  case Pecos::HISTOGRAM_PT_STRING:
    return HISTOGRAM_POINT_UNCERTAIN_STRING; break;
  case Pecos::HISTOGRAM_PT_REAL:
    return HISTOGRAM_POINT_UNCERTAIN_REAL; break;
  // continuous epistemic
  case Pecos::CONTINUOUS_INTERVAL_UNCERTAIN:
    return CONTINUOUS_INTERVAL_UNCERTAIN; break;
  // discrete epistemic
  case Pecos::DISCRETE_INTERVAL_UNCERTAIN:
    return DISCRETE_INTERVAL_UNCERTAIN; break;
  case Pecos:: DISCRETE_UNCERTAIN_SET_INT:
    return DISCRETE_UNCERTAIN_SET_INT; break;
  case Pecos::DISCRETE_UNCERTAIN_SET_STRING:
    return DISCRETE_UNCERTAIN_SET_STRING; break;
  case Pecos::DISCRETE_UNCERTAIN_SET_REAL:
    return DISCRETE_UNCERTAIN_SET_REAL; break;
  default:
    Cerr << "Error: unsupported Pecos distribution type in "
         << "pecos_to_dakota_variable_type()." << std::endl;
    abort_handler(MODEL_ERROR);  return 0;  break;
  }
}


void ProbabilityTransformModel::
resp_x_to_u_mapping(const Variables& x_vars,     const Variables& u_vars,
                    const Response&  x_response, Response&        u_response)
{
  const RealVector&         x_cv      = x_vars.continuous_variables();
  SizetMultiArrayConstView  x_cv_ids  = x_vars.continuous_variable_ids();
  SizetMultiArrayConstView  x_acv_ids = x_vars.all_continuous_variable_ids();
  const RealVector&         x_fns     = x_response.function_values();

  // In this recasting, the inputs and outputs are mapped one-to-one, with no
  // reordering.  However, the x-space ASV may be augmented from the original
  // u-space ASV due to nonlinear mapping logic in RecastModel::transform_set().
  const ShortArray& u_asv = u_response.active_set_request_vector();
  const SizetArray& u_dvv = u_response.active_set_derivative_vector();
  const ShortArray& x_asv = x_response.active_set_request_vector();
  const SizetArray& x_dvv = x_response.active_set_derivative_vector();
  size_t i, j, num_fns = x_asv.size(), num_deriv_vars = x_dvv.size();
  if (u_asv.size() != num_fns) {
    Cerr << "Error: inconsistent response function definition in Probability"
	 << "TransformModel::resp_x_to_u_mapping().\n       x-space response "
	 << "size = " << num_fns << ", u-space response size =\n"
	 << u_asv.size() << std::endl;
    abort_handler(MODEL_ERROR);
  }
  if (!ptmInstance->xDist.correlation() && u_dvv != x_dvv) {
    Cerr << "Error: inconsistent derivative component definition in Probability"
	 << "TransformModel::resp_x_to_u_mapping().\nx-space DVV =\n" << x_dvv
         << "u-space DVV =\n" << u_dvv << std::endl;
    abort_handler(MODEL_ERROR);
  }
  bool u_grad_flag = false, u_hess_flag = false;
  for (i=0; i<num_fns; ++i) {
    if (u_asv[i] & 2)
      u_grad_flag = true;
    if (u_asv[i] & 4)
      u_hess_flag = true;
  }

  bool map_derivs = ( (u_grad_flag || u_hess_flag) &&
                      u_dvv != u_vars.inactive_continuous_variable_ids() );
  bool nonlinear_vars_map = ptmInstance->
    nonlinear_variables_mapping(ptmInstance->xDist, ptmInstance->uDist);
  RealVector   fn_grad_x,  fn_grad_us;  RealSymMatrix      fn_hess_us;
  RealMatrix jacobian_xu, jacobian_xs;  RealSymMatrixArray hessian_xu;

  if (map_derivs) {
    // The following transformation data is invariant w.r.t. the response fns
    // and is computed outside of the num_fns loop
    if (ptmInstance->distParamDerivs)
      ptmInstance->natafTransform.jacobian_dX_dS(x_cv, jacobian_xs,
          x_cv_ids, x_acv_ids, ptmInstance->primaryACVarMapIndices,
          ptmInstance->secondaryACVarMapTargets);
    else {
      if (u_grad_flag || u_hess_flag)
        ptmInstance->natafTransform.jacobian_dX_dU(x_cv, jacobian_xu);
      if (u_hess_flag && nonlinear_vars_map)
        ptmInstance->natafTransform.hessian_d2X_dU2(x_cv, hessian_xu);
    }
  }

  for (i=0; i<num_fns; ++i) {
    short u_asv_val = u_asv[i]; // original request from iterator
    short x_asv_val = x_asv[i]; // mapped request for sub-model

    // map value g(x) to G(u)
    if (u_asv_val & 1) {
      if ( !(x_asv_val & 1) ) {
        Cerr << "Error: missing required sub-model data in ProbabilityTransform"
	     << "Model::resp_x_to_u_mapping()" << std::endl;
        abort_handler(MODEL_ERROR);
      }
      // no transformation: g(x) = G(u) by definition
      u_response.function_value(x_fns[i], i);
    }

    // manage data requirements for derivative transformations: if fn_grad_x
    // is needed for Hessian x-form (nonlinear I/O mapping), then x_asv has been
    // augmented to include the gradient in RecastModel::transform_set().
    if (x_asv_val & 2)
      fn_grad_x = x_response.function_gradient_view(i);

    // map gradient dg/dx to dG/du
    if (u_asv_val & 2) {
      if ( !(x_asv_val & 2) ) {
        Cerr << "Error: missing required gradient sub-model data in Probability"
	     << "TransformModel::resp_x_to_u_mapping()" << std::endl;
        abort_handler(MODEL_ERROR);
      }
      if (map_derivs) { // perform transformation
        fn_grad_us = u_response.function_gradient_view(i);
        if (ptmInstance->distParamDerivs) // transform subset of components
          ptmInstance->natafTransform.trans_grad_X_to_S(fn_grad_x,
              fn_grad_us, jacobian_xs, x_dvv, x_cv_ids, x_acv_ids,
              ptmInstance->primaryACVarMapIndices,
              ptmInstance->secondaryACVarMapTargets);
        else   // transform subset of components
          ptmInstance->natafTransform.trans_grad_X_to_U(fn_grad_x,
              fn_grad_us, jacobian_xu, x_dvv, x_cv_ids);
      }
      else // no transformation: dg/dx = dG/du
        u_response.function_gradient(fn_grad_x, i);
    }

    // map Hessian d^2g/dx^2 to d^2G/du^2
    if (u_asv_val & 4) {
      if ( !(x_asv_val & 4) ||
           ( map_derivs && nonlinear_vars_map && !(x_asv_val & 2) ) ) {
        Cerr << "Error: missing required sub-model data in ProbabilityTransform"
	     << "Model::resp_x_to_u_mapping()" << std::endl;
        abort_handler(MODEL_ERROR);
      }
      const RealSymMatrix& fn_hess_x = x_response.function_hessian(i);
      if (map_derivs) { // perform transformation
        fn_hess_us = u_response.function_hessian_view(i);
        if (ptmInstance->distParamDerivs) { // transform subset of components
          Cerr << "Error: Hessians with respect to inserted variables not yet "
               << "supported." << std::endl;
          abort_handler(MODEL_ERROR);
          //ptmInstance->natafTransform.trans_hess_X_to_S(fn_hess_x,
          //  fn_hess_us, jacobian_xs, hessian_xs, fn_grad_s, x_dvv,
          //  x_cv_ids, x_vars.all_continuous_variable_ids(),
          //  ptmInstance->primaryACVarMapIndices,
          //  ptmInstance->secondaryACVarMapTargets);
        }
	else // transform subset of components
          ptmInstance->natafTransform.trans_hess_X_to_U(fn_hess_x, fn_hess_us,
              jacobian_xu, hessian_xu, fn_grad_x, x_dvv, x_cv_ids);
      }
      else // no transformation: d^2g/dx^2 = d^2G/du^2
        u_response.function_hessian(fn_hess_x, i);
    }
  }

#ifdef DEBUG
  Cout << "\nx_response:\n" << x_response
       << "\nu_response:\n" << u_response << std::endl;
#endif
}


/** Define the DVV for x-space derivative evaluations by augmenting
    the iterator requests to account for correlations. */
void ProbabilityTransformModel::
set_u_to_x_mapping(const Variables& u_vars, const ActiveSet& u_set,
		   ActiveSet& x_set)
{
  //if (ptmInstance->distParamDerivs) {
  //}
  //else
  if (ptmInstance->xDist.correlation()) {
    const SizetArray& u_dvv = u_set.derivative_vector();
    SizetMultiArrayConstView cv_ids = u_vars.continuous_variable_ids();
    SizetMultiArrayConstView icv_ids
      = u_vars.inactive_continuous_variable_ids();
    bool std_dvv = (u_dvv == cv_ids || u_dvv == icv_ids);
    if (!std_dvv) { // partial random variable derivatives: check correlations
      SizetMultiArrayConstView acv_ids = u_vars.all_continuous_variable_ids();
      size_t i, j, num_cv = cv_ids.size(), num_acv = acv_ids.size();
      SizetArray x_dvv;
      const RealSymMatrix& corr_x = ptmInstance->xDist.correlation_matrix();
      for (i=0; i<num_acv; ++i) { // insert in ascending order
        size_t acv_id = acv_ids[i];
        if (contains(u_dvv, acv_id))
          x_dvv.push_back(acv_id);
        else {
          size_t cv_index = find_index(cv_ids, acv_id);
          if (cv_index != _NPOS) { // random var: check correlation
            for (j=0; j<num_cv; ++j) {
              if (cv_index != j &&
                  std::fabs(corr_x(cv_index, j)) > Pecos::SMALL_NUMBER &&
                  contains(u_dvv, cv_ids[j])) {
                x_dvv.push_back(acv_id);
                break;
              }
            }
          }
        }
      }
      x_set.derivative_vector(x_dvv);
    }
  }
}

}  // namespace Dakota
