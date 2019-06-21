/*  _______________________________________________________________________

    DAKOTA: Design Analysis Kit for Optimization and Terascale Applications
    Copyright 2014 Sandia Corporation.
    This software is distributed under the GNU Lesser General Public License.
    For more information, see the README file in the top Dakota directory.
    _______________________________________________________________________ */

//- Class:	 NonD
//- Description: Base class for NonDeterministic branch
//- Owner:	 Mike Eldred
//- Checked by:
//- Version:

#ifndef DAKOTA_NOND_H
#define DAKOTA_NOND_H

#include "DakotaAnalyzer.hpp"

//#define DERIV_DEBUG


namespace Dakota {


/// Base class for all nondetermistic iterators (the DAKOTA/UQ branch).

/** The base class for nondeterministic iterators consolidates
    uncertain variable data and probabilistic utilities for inherited
    classes. */

class NonD: public Analyzer
{
public:

  //
  //- Heading: Member functions
  //

  /// set requestedRespLevels, requestedProbLevels, requestedRelLevels,
  /// requestedGenRelLevels, respLevelTarget, cdfFlag, and pdfOutput
  /// (used in combination with alternate ctors)
  void requested_levels(const RealVectorArray& req_resp_levels,
			const RealVectorArray& req_prob_levels,
			const RealVectorArray& req_rel_levels,
			const RealVectorArray& req_gen_rel_levels,
			short resp_lev_tgt, short resp_lev_tgt_reduce,
			bool cdf_flag, bool pdf_output);

  /// set distParamDerivs
  void distribution_parameter_derivatives(bool dist_param_derivs);

  /// prints the z/p/beta/beta* mappings reflected in
  /// {requested,computed}{Resp,Prob,Rel,GenRel}Levels for default
  /// qoi_type and qoi_labels
  void print_level_mappings(std::ostream& s) const;
  /// prints the z/p/beta/beta* mappings reflected in
  /// {requested,computed}{Resp,Prob,Rel,GenRel}Levels
  void print_level_mappings(std::ostream& s, String qoi_type,
			    const StringArray& qoi_labels) const;
  /// print level mapping statistics using optional pre-pend
  void print_level_mappings(std::ostream& s, const RealVector& level_maps,
			    bool moment_offset, const String& prepend = "");

  //
  //- Heading: Virtual member function redefinitions
  //

  bool resize();

  //
  //- Heading: Set/get routines
  //

  /// get pdfOutput
  bool pdf_output() const;
  /// set pdfOutput
  void pdf_output(bool output);

  /// get finalMomentsType
  short final_moments_type() const;
  /// set finalMomentsType
  void final_moments_type(short type);

protected:

  //
  //- Heading: Constructors and destructor
  //

  /// constructor
  NonD(ProblemDescDB& problem_db, Model& model);
  /// alternate constructor for sample generation and evaluation "on the fly"
  NonD(unsigned short method_name, Model& model);
  /// alternate constructor for sample generation "on the fly"
  NonD(unsigned short method_name, const RealVector& lower_bnds,
       const RealVector& upper_bnds);

  ~NonD(); ///< destructor

  //
  //- Heading: Virtual member function redefinitions
  //

  void derived_set_communicators(ParLevLIter pl_iter);

  void initialize_run();
  void pre_run();
  void finalize_run();

  // return the final uncertain variables from the nondeterministic iteration
  //const Variables& variables_results() const;
  /// return the final statistics from the nondeterministic iteration
  const Response& response_results() const;
  /// set the active set within finalStatistics
  void response_results_active_set(const ActiveSet& set);

  //
  //- Heading: New virtual member functions
  //

  /// initializes respCovariance
  virtual void initialize_response_covariance();
  /// initializes finalStatistics for storing NonD final results
  virtual void initialize_final_statistics();
  /// update finalStatistics::functionValues
  virtual void update_final_statistics();

  //
  //- Heading: Utility routines
  //

  /// concatenate computed{Resp,Prob,Rel,GenRel}Levels into level_maps
  void pull_level_mappings(RealVector& level_maps);
  /// update computed{Resp,Prob,Rel,GenRel}Levels from level_maps
  void push_level_mappings(const RealVector& level_maps);

  /// distribute pilot sample specification across model levels
  void load_pilot_sample(const SizetArray& pilot_spec, SizetArray& delta_N_l);
  /// distribute pilot sample specification across model forms or levels
  void load_pilot_sample(const SizetArray& pilot_spec, const Sizet3DArray& N_l,
			 SizetArray& delta_N_l);
  /// distribute pilot sample specification across model forms and levels
  void load_pilot_sample(const SizetArray& pilot_spec, const Sizet3DArray& N_l,
			 Sizet2DArray& delta_N_l);

  /// Size local variables
  void size();

  /// create a system-generated unique seed (when a seed is unspecified)
  int generate_system_seed();

  /// resizes finalStatistics::functionGradients based on finalStatistics ASV
  void resize_final_statistics_gradients();
  /// update finalStatistics::functionValues from momentStats and
  /// computed{Prob,Rel,GenRel,Resp}Levels
  void update_aleatory_final_statistics();
  /// update system metrics from component metrics within finalStatistics
  void update_system_final_statistics();
  /// update finalStatistics::functionGradients
  void update_system_final_statistics_gradients();

  /// size computed{Resp,Prob,Rel,GenRel}Levels
  void initialize_level_mappings();
  /// compute the PDF bins from the CDF/CCDF values and store in
  /// computedPDF{Abscissas,Ordinates}
  void compute_densities(const RealRealPairArray& min_max_fns,
			 bool prob_refinement = false,
			 bool all_levels_computed = false);
  /// output the PDFs reflected in computedPDF{Abscissas,Ordinates} using
  /// default qoi_type and pdf_labels
  void print_densities(std::ostream& s) const;
  /// output the PDFs reflected in computedPDF{Abscissas,Ordinates}
  void print_densities(std::ostream& s, String qoi_type,
		       const StringArray& pdf_labels) const;
  /// print system series/parallel mappings for response levels
  void print_system_mappings(std::ostream& s) const;

  /// print evaluation summary for multilevel sampling across 1D profile
  void print_multilevel_evaluation_summary(std::ostream& s,
					   const SizetArray& N_samp);
  /// print evaluation summary for multilevel sampling across 2D profile
  void print_multilevel_evaluation_summary(std::ostream& s,
					   const Sizet2DArray& N_samp);
  /// print evaluation summary for multilevel sampling across 3D profile
  void print_multilevel_evaluation_summary(std::ostream& s,
					   const Sizet3DArray& N_samp);

  /// recast x_model from x-space to u-space to create u_model
  void transform_model(Model& x_model, Model& u_model, short u_space_type,
		       bool truncated_bounds = false, Real bound = 10.);

  /// assign a NonDLHSSampling instance within u_space_sampler
  void construct_lhs(Iterator& u_space_sampler, Model& u_model,
		     unsigned short sample_type, int num_samples, int seed,
		     const String& rng, bool vary_pattern,
		     short sampling_vars_mode = ACTIVE);

  /// compute a one-sided sample increment for multilevel methods to
  /// move current sampling level to a new target
  size_t one_sided_delta(Real current, Real target);

  /// allocate results array storage for distribution mappings
  void archive_allocate_mappings();
  /// archive the mappings from specified response levels for specified fn
  void archive_from_resp(size_t fn_index, size_t inc_id = 0);
  /// archive the mappings to computed response levels for specified fn and
  /// (optional) increment id.
  void archive_to_resp(size_t fn_index, size_t inc_id = 0);
  /// allocate results array storage for pdf histograms
  void archive_allocate_pdf();
  /// archive a single pdf histogram for specified function
  void archive_pdf(size_t fn_index, size_t inc_id = 0);

  //
  //- Heading: Data members
  //

  /// pointer to the active object instance used within static evaluator
  /// functions in order to avoid the need for static data
  static NonD* nondInstance;
  /// pointer containing previous value of nondInstance
  NonD* prevNondInstance;

  // The following variable counts reflect the native Model space, which could
  // correspond to either X or U space.  If a specific X or U variables count
  // is needed, then natafTransform.ranVarTypesX/U.count() should be used.

  /*
  /// number of continuous design variables (modeled using uniform
  /// distribution for All view modes)
  size_t numContDesVars;
  /// number of discrete integer design variables (modeled using discrete
  /// histogram distributions for All view modes)
  size_t numDiscIntDesVars;
  /// number of discrete string design variables (modeled using discrete
  /// histogram distributions for All view modes)
  size_t numDiscStringDesVars;
  /// number of discrete real design variables (modeled using discrete
  /// histogram distributions for All view modes)
  size_t numDiscRealDesVars;
  /// total number of design variables
  size_t numDesignVars;
  /// number of continuous state variables (modeled using uniform
  /// distribution for All view modes)
  size_t numContStateVars;
  /// number of discrete integer state variables (modeled using discrete
  /// histogram distributions for All view modes)
  size_t numDiscIntStateVars;
  /// number of discrete string state variables (modeled using discrete
  /// histogram distributions for All view modes)
  size_t numDiscStringStateVars;
  /// number of discrete real state variables (modeled using discrete
  /// histogram distributions for All view modes)
  size_t numDiscRealStateVars;
  /// total number of state variables
  size_t numStateVars;

  /// number of normal uncertain variables (native space)
  size_t numNormalVars;
  /// number of lognormal uncertain variables (native space)
  size_t numLognormalVars;
  /// number of uniform uncertain variables (native space)
  size_t numUniformVars;
  /// number of loguniform uncertain variables (native space)
  size_t numLoguniformVars;
  /// number of triangular uncertain variables (native space)
  size_t numTriangularVars;
  /// number of exponential uncertain variables (native space)
  size_t numExponentialVars;
  /// number of beta uncertain variables (native space)
  size_t numBetaVars;
  /// number of gamma uncertain variables (native space)
  size_t numGammaVars;
  /// number of gumbel uncertain variables (native space)
  size_t numGumbelVars;
  /// number of frechet uncertain variables (native space)
  size_t numFrechetVars;
  /// number of weibull uncertain variables (native space)
  size_t numWeibullVars;
  /// number of histogram bin uncertain variables (native space)
  size_t numHistogramBinVars;
  /// number of Poisson uncertain variables (native space)
  size_t numPoissonVars;
  /// number of binomial uncertain variables (native space)
  size_t numBinomialVars;
  /// number of negative binomial uncertain variables (native space)
  size_t numNegBinomialVars;
  /// number of geometric uncertain variables (native space)
  size_t numGeometricVars;
  /// number of hypergeometric uncertain variables (native space)
  size_t numHyperGeomVars;
  /// number of histogram point integer uncertain variables (native space)
  size_t numHistogramPtIntVars;
  /// number of histogram point string uncertain variables (native space)
  size_t numHistogramPtStringVars;
  /// number of histogram point real uncertain variables (native space)
  size_t numHistogramPtRealVars;
  /// number of continuous interval uncertain variables (native space)
  size_t numContIntervalVars;
  /// number of discrete interval uncertain variables (native space)
  size_t numDiscIntervalVars;
  /// number of discrete integer set uncertain variables (native space)
  size_t numDiscSetIntUncVars;
  /// number of discrete integer set uncertain variables (native space)
  size_t numDiscSetStringUncVars;
  /// number of discrete real set uncertain variables (native space)
  size_t numDiscSetRealUncVars;

  /// total number of continuous aleatory uncertain variables (native space)
  size_t numContAleatUncVars;
  /// total number of discrete integer aleatory uncertain variables
  /// (native space)
  size_t numDiscIntAleatUncVars;
  /// total number of discrete string aleatory uncertain variables
  /// (native space)
  size_t numDiscStringAleatUncVars;
  /// total number of discrete real aleatory uncertain variables (native space)
  size_t numDiscRealAleatUncVars;
  /// total number of aleatory uncertain variables (native space)
  size_t numAleatoryUncVars;
  /// total number of continuous epistemic uncertain variables (native space)
  size_t numContEpistUncVars;
  /// total number of discrete integer epistemic uncertain variables
  /// (native space)
  size_t numDiscIntEpistUncVars;
  /// total number of discrete string epistemic uncertain variables
  /// (native space)
  size_t numDiscStringEpistUncVars;
  /// total number of discrete real epistemic uncertain variables (native space)
  size_t numDiscRealEpistUncVars;
  /// total number of epistemic uncertain variables (native space)
  size_t numEpistemicUncVars;
  /// total number of uncertain variables (native space)
  size_t numUncertainVars;
  */

  /// flag for computing interval-type metrics instead of integrated metrics
  /// If any epistemic vars are active in a metric evaluation, then flag is set.
  bool epistemicStats;

  /// standardized or central resp moments, as determined by finalMomentsType.
  /// Calculated in compute_moments()) and indexed as (moment,fn).
  RealMatrix momentStats;

  // map response level z -> probability level p, reliability level beta,
  // or generalized reliability level beta*

  /// requested response levels for all response functions
  RealVectorArray requestedRespLevels;
  /// output probability levels for all response functions resulting
  /// from requestedRespLevels
  RealVectorArray computedProbLevels;
  /// output reliability levels for all response functions resulting
  /// from requestedRespLevels
  RealVectorArray computedRelLevels;
  /// output generalized reliability levels for all response functions
  /// resulting from requestedRespLevels
  RealVectorArray computedGenRelLevels;
  /// indicates mapping of z->p (PROBABILITIES), z->beta (RELIABILITIES),
  /// or z->beta* (GEN_RELIABILITIES)
  short respLevelTarget;
  /// indicates component or system series/parallel failure metrics
  short respLevelTargetReduce;

  // map probability level p, reliability level beta, or generalized
  // reliability level beta* -> response level z

  /// requested probability levels for all response functions
  RealVectorArray requestedProbLevels;
  /// requested reliability levels for all response functions
  RealVectorArray requestedRelLevels;
  /// requested generalized reliability levels for all response functions
  RealVectorArray requestedGenRelLevels;
  /// output response levels for all response functions resulting from
  /// requestedProbLevels, requestedRelLevels, or requestedGenRelLevels
  RealVectorArray computedRespLevels;

  /// total number of levels specified within requestedRespLevels,
  /// requestedProbLevels, and requestedRelLevels
  size_t totalLevelRequests;

  /// flag for type of probabilities/reliabilities used in mappings:
  /// cumulative/CDF (true) or complementary/CCDF (false)
  bool cdfFlag;

  /// flag for managing output of response probability density functions (PDFs)
  bool pdfOutput;
  /// sorted response PDF intervals bounds extracted from min/max sample
  /// and requested/computedRespLevels (vector lengths = num bins + 1)
  RealVectorArray computedPDFAbscissas;
  /// response PDF densities computed from bin counts divided by
  /// (unequal) bin widths (vector lengths = num bins)
  RealVectorArray computedPDFOrdinates;

  /// final statistics from the uncertainty propagation used in strategies:
  /// response means, standard deviations, and probabilities of failure
  Response finalStatistics;
  /// type of moments logged within finalStatistics: none, central, standard
  short finalMomentsType;

  /// index for the active ParallelLevel within ParallelConfiguration::miPLIters
  size_t miPLIndex;

  /// Whether PDF was computed for function i; used to determine whether
  /// a pdf should be archived
  BitArray pdfComputed;

private:

  /// convenience function for distributing a vector of levels among multiple
  /// response functions if a short-hand specification is employed.
  void distribute_levels(RealVectorArray& levels, bool ascending = true);

  /// Write level mappings to a file for a single response
  void level_mappings_file(size_t fn_index, const String& qoi_label) const;

  /// Print level mapping for a single response function to ostream
  void print_level_map(std::ostream& s, size_t fn_index,
		       const String& qoi_label) const;

  /// return true if N_l has consistent values
  bool homogeneous(const SizetArray& N_l) const;

  //
  //- Heading: Data members
  //

  /// flags calculation of derivatives with respect to distribution
  /// parameters s within resp_x_to_u_mapping() using the chain rule
  /// df/dx dx/ds.  The default is to calculate derivatives with respect
  /// to standard random variables u using the chain rule df/dx dx/du.
  bool distParamDerivs;
};


inline NonD::~NonD()
{ }


inline void NonD::distribution_parameter_derivatives(bool dist_param_derivs)
{ distParamDerivs = dist_param_derivs; }


inline bool NonD::pdf_output() const
{ return pdfOutput; }


inline void NonD::pdf_output(bool output)
{ pdfOutput = output; }


inline short NonD::final_moments_type() const
{ return finalMomentsType; }


inline void NonD::final_moments_type(short type)
{ finalMomentsType = type; }


inline void NonD::initialize_run()
{ 
  Analyzer::initialize_run();
  prevNondInstance = nondInstance; 
  nondInstance = this; 
}


inline void NonD::pre_run()
{ Analyzer::pre_run(); }


inline void NonD::finalize_run()
{ 
  nondInstance = prevNondInstance;
  Analyzer::finalize_run();
}


inline const Response& NonD::response_results() const
{ return finalStatistics; }


inline void NonD::response_results_active_set(const ActiveSet& set)
{ finalStatistics.active_set(set); }


inline void NonD::print_level_mappings(std::ostream& s) const
{
  print_level_mappings(s, "response function", iteratedModel.response_labels());
}


inline void NonD::print_densities(std::ostream& s) const
{ print_densities(s, "response function", iteratedModel.response_labels()); }


inline bool NonD::homogeneous(const SizetArray& N_l) const
{
  size_t N0 = N_l[0], i, len = N_l.size();
  for (i=1; i<len; ++i)
    if (N_l[i] != N0)
      return false;
  return true;
}


inline size_t NonD::one_sided_delta(Real current, Real target)
{ return (target > current) ? (size_t)std::floor(target - current + .5) : 0; }

} // namespace Dakota

#endif
