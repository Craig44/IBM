/**
 * @file Observation.cpp
 * @author  Scott Rasmussen (scott.rasmussen@zaita.com)
 * @version 1.0
 * @date 6/03/2013
 * @section LICENSE
 *
 * Copyright NIWA Science �2013 - www.niwa.co.nz
 *
 * $Date: 2008-03-04 16:33:32 +1300 (Tue, 04 Mar 2008) $
 */

// Headers
#include "Observation.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/join.hpp>

#include "Likelihoods/Manager.h"
#include "Model/Managers.h"
#include "Model/Model.h"

// Namespaces
namespace niwa {

/**
 * Default Constructor
 */

Observation::Observation(Model* model) : model_(model) {
  parameters_.Bind<string>(PARAM_LABEL, &label_, "Label", "");
  parameters_.Bind<string>(PARAM_TYPE, &type_, "Type of observation", "");
  parameters_.Bind<string>(PARAM_SIMULATION_LIKELIHOOD, &simulation_likelihood_label_, "Simulation likelihood to use", "");
}

/**
 * Validate the parameters passed in from the
 * configuration file
 */
void Observation::Validate() {
  LOG_TRACE();
  parameters_.Populate(model_);
  LOG_FINEST() << "validating obs " << label_ << " of type = " << type_;

  DoValidate();
}

/**
 * Build all of the runtime relationships required for
 * this observation
 */
void Observation::Build() {
  LOG_TRACE();

  likelihood_ = model_->managers().likelihood()->GetOrCreateLikelihood(model_, label_, simulation_likelihood_label_);
  if (!likelihood_) {
    LOG_FATAL_P(PARAM_SIMULATION_LIKELIHOOD) << "(" << simulation_likelihood_label_ << ") could not be found or constructed.";
    return;
  }
  if (std::find(allowed_likelihood_types_.begin(), allowed_likelihood_types_.end(), likelihood_->type()) == allowed_likelihood_types_.end()) {
    string allowed = boost::algorithm::join(allowed_likelihood_types_, ", ");
    LOG_FATAL_P(PARAM_SIMULATION_LIKELIHOOD) << ": likelihood " << likelihood_->type() << " is not supported by the " << type_ << " observation."
        << " Allowed types are: " << allowed;
  }

  DoBuild();
}

/**
 * Reset our observation so it can be called again
 */
void Observation::Reset() {
  comparisons_.clear();

  DoReset();
}

/**
 * Save the comparison that was done during an observation to the list of comparisons. Each comparison contributes part to a score
 * and we will need to know what those parts are when reporting.
 *
 * @param age The age of the population being compared
 * @param expected The value generated by the model
 * @param observed The value passed in from the configuration file
 * @param error_value The error value for this comparison
 */
void Observation::SaveComparison(unsigned age, float length, string row_col, float expected, float simulated, float error_value, unsigned year){
  observations::Comparison new_comparison;
  new_comparison.age_ = age;
  new_comparison.length_ = length;
  new_comparison.expected_ = expected;
  new_comparison.simulated_ = simulated;
  new_comparison.error_value_ = error_value;
  comparisons_[year][row_col].push_back(new_comparison);
}

/**
 * Save the comparison that was done during an observation to the list of comparisons. Each comparison contributes part to a score
 * and we will need to know what those parts are when reporting.
 *
 * @param age The age of the population being compared
 * @param expected The value generated by the model
 * @param observed The value passed in from the configuration file
 * @param error_value The error value for this comparison
 */
void Observation::SaveComparison(unsigned age, unsigned sex, float length, string row_col, float expected, float simulated, float error_value, unsigned year){
  observations::Comparison new_comparison;
  new_comparison.age_ = age;
  new_comparison.sex_ = sex;
  new_comparison.length_ = length;
  new_comparison.expected_ = expected;
  new_comparison.simulated_ = simulated;
  new_comparison.error_value_ = error_value;
  comparisons_[year][row_col].push_back(new_comparison);
}
} /* namespace niwa */
