/*  _______________________________________________________________________

    DAKOTA: Design Analysis Kit for Optimization and Terascale Applications
    Copyright 2014 Sandia Corporation.
    This software is distributed under the GNU Lesser General Public License.
    For more information, see the README file in the top Dakota directory.
    _______________________________________________________________________ */

//- Class:       APPSEvalMgr
//- Description: Specialized evaluation manager class derived from
//-              APPSPACK's Executor class which redefines virtual
//-              evaluation functions with DAKOTA's response
//-              computation procedures
//- Owner:       Patty Hough
//- Checked by:
//- Version: $Id$

#include "APPSEvalMgr.hpp"

namespace Dakota {

/// Evaluation manager class for APPSPACK 

/** The APPSEvalMgr class is derived from APPSPACK's Executor class.
    It implements the methods of that class in such away that allows
    DAKOTA to manage the computation of responses instead of APPS.
    Iterate and response values are passed between Dakota and APPSPACK
    via this interface. */

APPSEvalMgr::APPSEvalMgr(Model& model) :
  iteratedModel(model), modelAsynchFlag(1), blockingSynch(0), numWorkersUsed(0),
  numWorkersTotal(1), xTrial(model.continuous_variables())
{
  // don't use the probDescDB so that this ctor may be used with both
  // the standard and on-the-fly APPSOptimizer ctors

  // placeholder for more complexity if needed later
}

/** Check to see if all processors available for function evaluations
    are being used.  If not, tell APPS that one is available. */
bool APPSEvalMgr::isReadyForWork() const
{
  if (numWorkersUsed < numWorkersTotal) return true;

  return false;
}

/** Convert APPSPACK vector of variables to DAKOTA vector of variables
    and perform function evaluation asynchronously or not as specified
    in the DAKOTA input deck.  If evaluation is asynchronous, map the
    dakota id to the APPS tag.  If evaluation is synchronous, map the
    responses to the APPS tag. */
bool APPSEvalMgr::submit(const int apps_tag, const HOPSPACK::Vector& apps_xtrial,
			 const HOPSPACK::EvalRequestType apps_request)
{
  int num_cont_vars = iteratedModel.cv();
  int num_disc_int_vars = iteratedModel.div();
  int num_disc_real_vars = iteratedModel.drv();
  int num_disc_string_vars = iteratedModel.dsv();

  const BitArray& int_set_bits = iteratedModel.discrete_int_sets();
  const IntSetArray& set_int_vars = iteratedModel.discrete_set_int_values();
  const RealSetArray& set_real_vars = iteratedModel.discrete_set_real_values();
  const StringSetArray& set_string_vars = iteratedModel.discrete_set_string_values();

  size_t i, dsi_cntr;

  if (numWorkersUsed < numWorkersTotal) {

    for(i=0; i<num_cont_vars; i++)
      iteratedModel.continuous_variable(apps_xtrial[i], i);

    for(i=0, dsi_cntr=0; i<num_disc_int_vars; i++)
      { 
	// This active discrete int var is a set type
	// Map from index back to value.
	if (int_set_bits[i]) {
	  int int_set_value = 
	    set_index_to_value(apps_xtrial[i+num_cont_vars], set_int_vars[dsi_cntr]);
	  iteratedModel.discrete_int_variable(int_set_value, i);
	  ++dsi_cntr;
	}
	// This active discrete int var is a range type
	else
	  iteratedModel.discrete_int_variable(apps_xtrial[i+num_cont_vars], i);
      }

    for (i=0; i<num_disc_real_vars; i++)
    {
      Real real_set_value = 
	set_index_to_value(apps_xtrial[i+num_cont_vars+num_disc_int_vars], set_real_vars[i]);
      iteratedModel.discrete_real_variable(real_set_value, i);
    }

    for (i=0; i<num_disc_string_vars; i++)
      iteratedModel.discrete_string_variable(set_index_to_value(apps_xtrial[i+num_cont_vars+num_disc_int_vars+num_disc_real_vars], set_string_vars[i]), i);

    numWorkersUsed++;
  }
  else
    return false;

  //
  // Compute response
  //

  if (modelAsynchFlag) {
    // Need to map between DAKOTA and APPS tags for asynchronous
    // evaluations.

    iteratedModel.asynch_compute_response();
    tagList[iteratedModel.evaluation_id()] = apps_tag;
  }
  else {
    // Need to associate responses with APPS tags for synchronous
    // evaluations.

    iteratedModel.compute_response();
    functionList[apps_tag] = iteratedModel.current_response().function_values();
  }

  return true;
}

/** Retrieve a set of reponse values, convert to APPS data structures,
    and return them to APPS.  APPS tags are tied to corresponding
    responses using the appropriate (i.e., asynchronous or
    synchronous) map. */
int APPSEvalMgr::recv(int& apps_tag, HOPSPACK::Vector& apps_f,
		      HOPSPACK::Vector& apps_cEqs, HOPSPACK::Vector& apps_cIneqs,
		      string& apps_msg)
{
  // APPSPACK only wants one completion, so buffer multiple DAKOTA
  // completions in dakotaResponseMap and only call DAKOTA synchronize
  // functions when the list has been exhausted.  Each call to
  // synchronize_nowait returns a fresh set of jobs (i.e., returned
  // completions are removed from DAKOTA's lists).

  size_t numNonlinearEqConstraints
    = iteratedModel.num_nonlinear_eq_constraints();
  // Any MOO/NLS recasting is responsible for setting the scalar min/max
  // sense within the recast.
  const BoolDeque& sense = iteratedModel.primary_response_fn_sense();
  bool max_flag = (!sense.empty() && sense[0]);

  if (modelAsynchFlag) {

    if (dakotaResponseMap.empty())
      dakotaResponseMap = (blockingSynch) ?
	iteratedModel.synchronize() : iteratedModel.synchronize_nowait();

    // Grab the first response (asynchronous) and map from DAKOTA to
    // APPS.  Note that this includes mapping the constraints using
    // the transformation defined in APPSOptimizer.

    if (!dakotaResponseMap.empty()) { // synchronize_nowait may return none

      IntRespMCIter response_iter = dakotaResponseMap.begin();
      int dakota_id = response_iter->first;
      std::map<int,int>::iterator find_tag = tagList.find(dakota_id);

      if (find_tag != tagList.end()) {
	const RealVector& local_fn_vals
	  = response_iter->second.function_values();
	apps_f.resize(1);
	apps_cEqs.resize(numNonlinearEqConstraints);
	apps_cIneqs.resize(constrMapIndices.size()-numNonlinearEqConstraints);
	apps_f[0] = (max_flag) ? -local_fn_vals[0] : local_fn_vals[0];
	for (int i=0; i<apps_cEqs.size(); i++)
	  apps_cEqs[i] = constrMapOffsets[i] +
	    constrMapMultipliers[i]*local_fn_vals[constrMapIndices[i]+1];
	for (int i=0; i<apps_cIneqs.size(); i++)
	  apps_cIneqs[i] = constrMapOffsets[i+numNonlinearEqConstraints] +
	    constrMapMultipliers[i+numNonlinearEqConstraints] * 
	    local_fn_vals[constrMapIndices[i+numNonlinearEqConstraints]+1];
	apps_tag = (*find_tag).second;
	apps_msg = "success";
	dakotaResponseMap.erase(dakota_id);
	tagList.erase(find_tag);
	numWorkersUsed--;
	return dakota_id;
      }
      else {
	dakotaResponseMap.clear();
	return 0;
      }
    }
    else
      return 0;
  }
  else {
    // Grab the first response (synchronous) and map from DAKOTA to
    // APPS.  Note that this includes mapping the constraints using
    // the transformation defined in APPSOptimizer.

    if (!functionList.empty()) {
      std::map<int, RealVector>::iterator f_iter = functionList.begin();
      const RealVector& local_fn_vals = f_iter->second;
      apps_f.resize(1);
      apps_cEqs.resize(numNonlinearEqConstraints);
      apps_cIneqs.resize(constrMapIndices.size()-numNonlinearEqConstraints);
      apps_f[0] = (max_flag) ? -local_fn_vals[0] : local_fn_vals[0];
      for (int i=0; i<apps_cEqs.size(); i++)
	apps_cEqs[i] = constrMapOffsets[i] +
	  constrMapMultipliers[i]*local_fn_vals[constrMapIndices[i]+1];
      for (int i=0; i<apps_cIneqs.size(); i++)
	apps_cIneqs[i] = constrMapOffsets[i+numNonlinearEqConstraints] +
	  constrMapMultipliers[i+numNonlinearEqConstraints] * 
	  local_fn_vals[constrMapIndices[i+numNonlinearEqConstraints]+1];
      apps_tag = (*f_iter).first;
      apps_msg = "success";
      functionList.erase(f_iter);
      numWorkersUsed--;
      return (apps_tag+1);
    }
    else
      return 0;
  }
}

std::string APPSEvalMgr::getEvaluatorType(void) const
{
  return ("DakotaLinkedEvaluator");
}

} // namespace Dakota
