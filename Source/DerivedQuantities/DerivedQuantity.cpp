/**
 * @file DerivedQuantity.cpp
 * @author  Scott Rasmussen (scott.rasmussen@zaita.com)
 * @date 4/07/2013
 * @section LICENSE
 *
 * Copyright NIWA Science �2013 - www.niwa.co.nz
 *
 */

// headers
#include "DerivedQuantity.h"

#include <limits>

#include "TimeSteps/Manager.h"

// namespaces
namespace niwa {

/**
 * Default constructor
 *
 * Bind any parameters that are allowed to be loaded from the configuration files.
 * Set bounds on registered parameters
 * Register any parameters that can be an estimated or utilised in other run modes (e.g profiling, yields, projections etc)
 * Set some initial values
 *
 * Note: The constructor is parsed to generate Latex for the documentation.
 */
DerivedQuantity::DerivedQuantity(Model* model)
  : model_(model){
  parameters_.Bind<string>(PARAM_LABEL, &label_, "Label of the derived quantity", "");
  parameters_.Bind<string>(PARAM_TYPE, &type_, "Type of derived quantity", "");
  parameters_.Bind<string>(PARAM_TIME_STEP, &time_step_label_, "The time step in which to calculate the derived quantity after", "");
  parameters_.Bind<float>(PARAM_PROPORTION_TRHOUGH_MORTALITY, &time_step_proportion_, "Proportion through the mortality block of the time step when calculated", "", float(0.5))->set_range(0.0, 1.0);
}

/**
 * Populate any parameters,
 * Validate values are within expected ranges when we cannot use bind<>() overloads
 *
 * Note: all parameters are populated from configuration files
 */
void DerivedQuantity::Validate() {
  parameters_.Populate(model_);

  DoValidate();
}

/**
 * Build any objects that will need to be utilised by this object.
 * Obtain smart_pointers to any objects that will be used by this object.
 */
void DerivedQuantity::Build() {
  LOG_TRACE();
  /**
   * ensure the time steps we have are valid
   */
  TimeStep* time_step = model_->managers().time_step()->GetTimeStep(time_step_label_);
  if (!time_step)
    LOG_FATAL_P(PARAM_TIME_STEP) << " (" << time_step_label_ << ") could not be found. Have you defined it?";
  time_step->SubscribeToBlock(this);
  time_step->SubscribeToInitialisationBlock(this);

  world_ = model_->world_view();
  if (!world_) {
    LOG_CODE_ERROR() << "!world_ something has gone wrong because we cna't generate pointer to the world.";
  }

  DoBuild();
}

/**
 * Reset our derived quantity values
 */
void DerivedQuantity::Reset() {
  if (model_->is_initialisation_being_re_run())
    initialisation_values_.clear();

  // initialise the values variable
  for (unsigned year = model_->start_year(); year <= model_->final_year(); ++year) {
    LOG_FINEST() << "year = " << year;
    values_[year] = 0.0;
  }
}

/**
 * Return the calculated value stored in this derived quantity
 * for the parameter year. If the year does not exist as a standard
 * value we'll calculate how many years to go back in to
 * the initialisation phase values.
 *
 * Note: We cannot go back more than 1 phase. If this condition
 * is triggered we return the first value from the phase instead
 * of going back.
 *
 * @param year The year to get the derived quantity value for.
 * @return The derived quantity value
 */
float DerivedQuantity::GetValue(unsigned year) {
  LOG_FINEST() << "get value for year: " << year;

  if (values_.find(year) != values_.end())
    return values_[year];
  if (initialisation_values_.size() == 0)
    return 0.0;

  // Calculate how many years to go back. At this point
  // either we're in the init phases or we're going back
  // in to the init phases.
  unsigned years_to_go_back = model_->start_year() - year;

  float result = 0.0;
  if (years_to_go_back == 0) {
    LOG_WARNING() << "Years to go back is 0 in derived quantity " << label_ << " when it shouldn't be";
    result = (*initialisation_values_.rbegin()->rbegin());
  } else if (initialisation_values_.rbegin()->size() > years_to_go_back) {
    result = initialisation_values_.rbegin()->at(initialisation_values_.rbegin()->size() - years_to_go_back);
  } else if (initialisation_values_.size() == 1) {
    result = (*initialisation_values_.rbegin()->begin()); // first value of last init phase
  } else {
    result = (*(initialisation_values_.rbegin() + 1)->begin()); // first value of last init phase
  }

  LOG_FINEST() << "years_to_go_back: " << years_to_go_back
      << "; year: " << year
      << "; result: " << result
      << "; .begin(): " << (*initialisation_values_.rbegin()->rbegin())
      << ": .size(): " << initialisation_values_.rbegin()->size();

  return result;
}

float DerivedQuantity::GetValue(unsigned year, unsigned row, unsigned col) {
  LOG_FINEST() << "get value for year: " << year;
  if (not spatial_)
    LOG_CODE_ERROR() << "not spatial_";

  if (values_by_space_.find(year) != values_by_space_.end())
    return values_by_space_[year][row][col];
  if (initialisation_values_by_space_.size() == 0)
    return 0.0;

  // Calculate how many years to go back. At this point
  // either we're in the init phases or we're going back
  // in to the init phases.
  unsigned years_to_go_back = model_->start_year() - year;

  float result = 0.0;
  if (years_to_go_back == 0) {
    result = (*(*initialisation_values_by_space_.rbegin())[row][col].rbegin());
  } else if ((*initialisation_values_by_space_.rbegin())[row][col].size() > years_to_go_back) {
    result =(*initialisation_values_by_space_.rbegin())[row][col].at((*initialisation_values_by_space_.rbegin())[row][col].size() - years_to_go_back);
  } else if (initialisation_values_by_space_.size() == 1) {
    result = (*(*initialisation_values_by_space_.rbegin())[row][col].begin()); // first value of last init phase
  } else {
    result = (*(*(initialisation_values_by_space_.rbegin() + 1))[row][col].begin()); // first value of last init phase
  }

  LOG_FINEST() << "years_to_go_back: " << years_to_go_back
      << "; year: " << year
      << "; result: " << result;
  return result;
}

/**
 * Return the last value stored for the target initialisation phase
 *
 * @param phase The index of the phase
 * @return The derived quantity value
 */
float DerivedQuantity::GetLastValueFromInitialisation(unsigned phase, unsigned row, unsigned col) {
  LOG_TRACE();
  if (initialisation_values_by_space_.size() <= phase)
    LOG_ERROR() << "No values have been calculated for the initialisation value in phase: " << phase;
  if (initialisation_values_by_space_[phase][row][col].size() == 0)
    LOG_ERROR() << "No values have been calculated for the initialisation value in phase: " << phase;

  LOG_FINE() << "returning value = " << *initialisation_values_by_space_[phase][row][col].rbegin();
  return *initialisation_values_by_space_[phase][row][col].rbegin();
}

/**
 * Return the last value stored for the target initialisation phase
 *
 * @param phase The index of the phase
 * @return The derived quantity value
 */
float DerivedQuantity::GetLastValueFromInitialisation(unsigned phase) {
  LOG_TRACE();
  LOG_FINEST() << "about to check init_values";

  if (initialisation_values_.size() <= phase)
    LOG_ERROR() << "No values have been calculated for the initialisation value in phase: " << phase << " This may mean you are asking for an SSB value before it has been calculated, check the order of your annual cycle";
  if (initialisation_values_[phase].size() == 0)
    LOG_ERROR() << "No values have been calculated for the initialisation value in phase: " << phase << " This may mean you are asking for an SSB value before it has been calculated, check the order of your annual cycle";


  LOG_FINE() << "returning value = " << *initialisation_values_[phase].rbegin();
  return *initialisation_values_[phase].rbegin();
}

/**
 * Return an initialisation value from a target phase with an index.
 * If no phases exist we return 0.0
 * If the phase contains no values return 0.0
 * If the phase doesn't have enough values for the index return the last value
 *
 * @param phase The index of the phase
 * @param index The index of the value in the phase
 * @return derived quantity value
 */
float DerivedQuantity::GetInitialisationValue(unsigned phase, unsigned index) {
  LOG_FINEST() << "phase = " << phase << "; index = " << index << "; initialisation_values_.size() = " << initialisation_values_.size();
  if (initialisation_values_.size() <= phase) {
    if (initialisation_values_.size() == 0)
      return 0.0;

    return (*initialisation_values_.rbegin()->rbegin());
  }

  LOG_FINEST() << "initialisation_values_[" << phase << "].size() = " << initialisation_values_[phase].size();
  if (initialisation_values_[phase].size() <= index) {
    if (initialisation_values_[phase].size() == 0)
      return 0.0;
    else
      return *initialisation_values_[phase].rbegin();
  }

  return initialisation_values_[phase][index];

  return 0.0;
}

float DerivedQuantity::GetInitialisationValue(unsigned row, unsigned col, unsigned phase, unsigned index) {
  LOG_FINEST() << "phase = " << phase << "; index = " << index << "; initialisation_values_.size() = " << initialisation_values_by_space_.size();
  if (initialisation_values_by_space_.size() <= phase) {
    if (initialisation_values_by_space_.size() == 0)
      return 0.0;

    return (*(*initialisation_values_by_space_.rbegin())[row][col].rbegin());
  }

  LOG_FINEST() << "initialisation_values_[" << phase << "].size() = " << initialisation_values_by_space_[phase][row][col].size();
  if (initialisation_values_by_space_[phase][row][col].size() <= index) {
    if (initialisation_values_by_space_[phase][row][col].size() == 0)
      return 0.0;
    else
      return *initialisation_values_by_space_[phase][row][col].rbegin();
  }

  return initialisation_values_by_space_[phase][row][col][index];

  return 0.0;
}
} /* namespace niwa */
