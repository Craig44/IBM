/**
 * @file ProcessRemovalsByAge.cpp
 * @author  C Marsh
 * @version 1.0
 * @date 25/07/18
 * @section LICENSE
 *
 */

// Headers
#include "ProcessRemovalsByAge.h"

#include <algorithm>

#include "Model/Model.h"
#include "Processes/Manager.h"
#include "Layers/Manager.h"
#include "AgeingErrors/Manager.h"
#include "Likelihoods/Manager.h"

#include "Utilities/Map.h"
#include "Utilities/Math.h"
#include "Utilities/To.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/join.hpp>
// Namespaces
namespace niwa {
namespace observations {

/**
 * Default constructor
 */
ProcessRemovalsByAge::ProcessRemovalsByAge(Model* model) : Observation(model) {
  error_values_table_ = new parameters::Table(PARAM_ERROR_VALUES);
  parameters_.Bind<unsigned>(PARAM_MIN_AGE, &min_age_, "Minimum age", "");
  parameters_.Bind<unsigned>(PARAM_MAX_AGE, &max_age_, "Maximum age", "");
  parameters_.Bind<bool>(PARAM_PLUS_GROUP, &plus_group_, "Use age plus group", "", true);
  parameters_.Bind<unsigned>(PARAM_YEARS, &years_, "Years for which there are observations", "");
  parameters_.Bind<string>(PARAM_AGEING_ERROR, &ageing_error_label_, "Label of ageing error to use", "", "");
  parameters_.Bind<string>(PARAM_PROCESS_LABEL, &process_label_, "Label of of removal process", "", "");
  parameters_.Bind<bool>(PARAM_NORMALISE, &are_obs_props_, "Are the compositions normalised to sum to one", "", true);
  parameters_.BindTable(PARAM_ERROR_VALUES, error_values_table_, "Table of error values of the observed values (note the units depend on the likelihood)", "", false);
  parameters_.Bind<string>(PARAM_LAYER_OF_CELLS, &layer_label_, "The layer that indicates what area to summarise observations over.", "");
  parameters_.Bind<string>(PARAM_CELLS, &cells_, "The cells we want to generate observations for from the layer of cells supplied", "");
  parameters_.Bind<string>(PARAM_SEX, &sexed_, "You can ask to 'ignore' sex (only option for unsexed model), or generate composition for a particular sex, either 'male' or 'female", "", PARAM_IGNORE)->set_allowed_values({PARAM_MALE,PARAM_FEMALE,PARAM_IGNORE});
  parameters_.Bind<string>(PARAM_SIMULATION_LIKELIHOOD, &simulation_likelihood_label_, "Simulation likelihood to use", "");

  allowed_likelihood_types_.push_back(PARAM_LOGNORMAL);
  allowed_likelihood_types_.push_back(PARAM_MULTINOMIAL);
  allowed_likelihood_types_.push_back(PARAM_DIRICHLET);
  allowed_likelihood_types_.push_back(PARAM_LOGISTIC_NORMAL);

}

/**
 * Destructor
 */
ProcessRemovalsByAge::~ProcessRemovalsByAge() {
  delete error_values_table_;
}

/**
 * Validate configuration file parameters
 */
void ProcessRemovalsByAge::DoValidate() {
  age_spread_ = (max_age_ - min_age_) + 1;
  /**
   * Do some simple checks
   */
  if (min_age_ < model_->min_age())
    LOG_ERROR_P(PARAM_MIN_AGE) << ": min_age (" << min_age_ << ") is less than the model's min_age (" << model_->min_age() << ")";
  if (max_age_ > model_->max_age())
    LOG_ERROR_P(PARAM_MAX_AGE) << ": max_age (" << max_age_ << ") is greater than the model's max_age (" << model_->max_age() << ")";

  for (auto year : years_) {
    LOG_FINEST() << "year : " << year;
  	if((year < model_->start_year()) || (year > model_->final_year()))
  		LOG_ERROR_P(PARAM_YEARS) << "Years can't be less than start_year (" << model_->start_year() << "), or greater than final_year (" << model_->final_year() << "). Please fix this.";
  }


  if (sexed_ == PARAM_IGNORE) {
    sexed_flag_ = false;
    sex_match_ = 0;
  } else if (sexed_ == PARAM_MALE) {
    sexed_flag_ = true;
    sex_match_ = 0;
  } else if (sexed_ == PARAM_FEMALE) {
    sexed_flag_ = true;
    sex_match_ = 1;
  }
  if(!model_->get_sexed()) {
    if (sexed_flag_)
      LOG_WARNING() << "you asked for a sexed observation but the model isn't sexed so I am ignoring this and giving you unsexed results.";
  }

  /**
   * Validate the number of obs provided matches age spread * category_labels * years
   * This is because we'll have 1 set of obs per category collection provided.
   * categories male+female male = 2 collections
   */
  unsigned obs_expected = age_spread_ + 1;

  /**
   * Build our error value map
   */
  vector<vector<string>>& error_values_data = error_values_table_->data();
  if (error_values_data.size() != years_.size()) {
    LOG_ERROR_P(PARAM_ERROR_VALUES) << " has " << error_values_data.size() << " rows defined, but we expected " << years_.size()
        << " to match the number of years provided";
  }

  for (vector<string>& error_values_data_line : error_values_data) {
    if (error_values_data_line.size() != 2 && error_values_data_line.size() != obs_expected) {
      LOG_FATAL_P(PARAM_ERROR_VALUES) << " has " << error_values_data_line.size() << " values defined, but we expected " << obs_expected
          << " to match the age speard  + 1 (for year)";
    }

    unsigned year = 0;
    if (!utilities::To<unsigned>(error_values_data_line[0], year))
      LOG_ERROR_P(PARAM_ERROR_VALUES) << " value " << error_values_data_line[0] << " could not be converted in to an unsigned integer. It should be the year for this line";
    if (std::find(years_.begin(), years_.end(), year) == years_.end())
      LOG_ERROR_P(PARAM_ERROR_VALUES) << " value " << year << " is not a valid year for this observation";
    for (unsigned i = 1; i < error_values_data_line.size(); ++i) {
      float value = 0;

      if (!utilities::To<float>(error_values_data_line[i], value))
        LOG_FATAL_P(PARAM_ERROR_VALUES) << " value (" << error_values_data_line[i] << ") could not be converted to a float";
      if (simulation_likelihood_label_ == PARAM_LOGNORMAL && value <= 0.0) {
        LOG_ERROR_P(PARAM_ERROR_VALUES) << ": error_value (" << value << ") cannot be equal to or less than 0.0";
      } else if (simulation_likelihood_label_ == PARAM_MULTINOMIAL && value < 0.0) {
        LOG_ERROR_P(PARAM_ERROR_VALUES) << ": error_value (" << value << ") cannot be less than 0.0";
      }

      error_values_by_year_[year].push_back(value);
    }
    if (error_values_by_year_[year].size() == 1) {
      error_values_by_year_[year].assign(obs_expected - 1, error_values_by_year_[year][0]);
    }
    LOG_FINEST() << "number of error values in year " << year << " = " << error_values_by_year_[year].size();
    if (error_values_by_year_[year].size() != obs_expected - 1)
      LOG_FATAL_P(PARAM_ERROR_VALUES) << "We counted " << error_values_by_year_[year].size() << " error values by year but expected " << obs_expected -1 << " based on the obs table";
  }

}

/**
 * Build any runtime relationships we may have and ensure
 * the labels for other objects are valid.
 */
void ProcessRemovalsByAge::DoBuild() {

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
  // Create a pointer to misclassification matrix
    if( ageing_error_label_ != "") {
      ageing_error_ = model_->managers().ageing_error()->GetAgeingError(ageing_error_label_);
      if (!ageing_error_)
        LOG_ERROR_P(PARAM_AGEING_ERROR) << "(" << ageing_error_label_ << ") could not be found. Have you defined it?";
    }
    if (ageing_error_label_ == "") {
      LOG_WARNING() << "You are suppling a an age based observation with no ageing_misclassification";
    }

    mortality_process_ = model_->managers().process()->GetMortalityProcess(process_label_);
    if (!mortality_process_)
      LOG_FATAL_P(PARAM_PROCESS_LABEL) << "could not find the process " << process_label_ << ", please make sure it exists";

    // Build and validate layers
    layer_ = model_->managers().layer()->GetCategoricalLayer(layer_label_);
    if (!layer_)
      LOG_FATAL_P(PARAM_LAYER_OF_CELLS) << "could not find layer " << layer_label_ << " does it exist?, if it exists is of type categorical?";

    // Check all the cells supplied are in the layer
    for (auto cell :  cells_) {
      LOG_FINEST() << "checking cell " << cell << " exists in layer";
      bool cell_found = false;
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          LOG_FINEST() << "checking row = " << row << " col = " << col << " value = " << layer_->get_value(row,col);
          if (layer_->get_value(row,col) == cell) {
            cell_found = true;
            break;
          }
        }
      }
      if (not cell_found)
        LOG_ERROR_P(PARAM_CELLS) << "could not find the cell '" << cell << "' in the layer " << layer_label_ << " please make sure that you supply cell labels that are consistent with the layer.";
    }

    if(not mortality_process_->check_years(years_)) {
      LOG_ERROR_P(PARAM_YEARS) << "there was a year that the mortality process doesn't not execute in, can you please check that the years you have supplied for this observation are years that the mortality process occurs in cheers.";
    }

    accumulated_age_frequency_.resize(model_->age_spread(),0.0);
}

/**
 * We have to have a pre-execute and execute but all the information is stored on the process so I am just going to do
 * all the calculations in teh Simulate call
 */
void ProcessRemovalsByAge::PreExecute() {

}

/**
 *
 */
void ProcessRemovalsByAge::Execute() {

}

/**
 * This method is called at the end of a model iteration
 * to generate simulated data
 */
void ProcessRemovalsByAge::Simulate() {
  LOG_MEDIUM() << "Simulating data for observation = " << label_;
  /**
   * Simulate or generate results
   * During simulation mode we'll simulate results for this observation
   */
  if (first_simualtion_run_) {
    vector<processes::composition_data>& age_frequency = mortality_process_->get_removals_by_age();
    LOG_FINE() << "number of elements for this process block = " << age_frequency.size();
    unsigned age_offset = min_age_ - model_->min_age();
    // iterate over all the years that we want
    bool cell_found = false;
    float biomass = 0.0;
    for (unsigned& year : years_) {
      for (string& cell : cells_) {
        biomass = 0.0;
        cell_found = false;
        fill(accumulated_age_frequency_.begin(), accumulated_age_frequency_.end(),0.0);
        for (auto age_comp_data : age_frequency) {
          if ((age_comp_data.year_ == year) && (layer_->get_value(age_comp_data.row_,age_comp_data.col_) == cell)) {
            // Lets accumulate the information for this cell and year
            LOG_FINE() << "row = " << age_comp_data.row_ << " col = " << age_comp_data.col_ << " cell = " << cell << " age comp = " << age_comp_data.frequency_.size();
            // check if sex specific observation
            if (sexed_flag_ & (sex_match_ == 1)) {
              for(unsigned i = 0; i < age_comp_data.frequency_.size(); ++i)
                accumulated_age_frequency_[i] += age_comp_data.female_frequency_[i];

            } else {
              for(unsigned i = 0; i < age_comp_data.frequency_.size(); ++i)
                accumulated_age_frequency_[i] += age_comp_data.frequency_[i];
            }
            cell_found = true;
            biomass += age_comp_data.biomass_ + age_comp_data.female_biomass_;
          }
        }
        LOG_FINE() << "before ageing error";
        if (not cell_found)
          continue; // to next cell
        if (ageing_error_) {
          vector<float> temp(model_->age_spread(), 0.0); // TODO remove this memory to build
          vector<vector<float>> &mis_matrix = ageing_error_->mis_matrix();
          for (unsigned i = 0; i < mis_matrix.size(); ++i) {
            for (unsigned j = 0; j < mis_matrix[i].size(); ++j) {
              temp[j] += accumulated_age_frequency_[i] * mis_matrix[i][j];
            }
          }
          for(unsigned i = 0; i < temp.size(); ++i)
            accumulated_age_frequency_[i] = temp[i];
        }

        /*
         *  Now collapse the number_age into the expected_values for the observation
         */
        float plus_group = 0.0;
        for (unsigned k = 0; k < model_->age_spread(); ++k) {
          if (k >= age_offset && (k - age_offset + min_age_) < max_age_)
            SaveComparison(k + model_->min_age(), 0, 0.0, biomass,cell, accumulated_age_frequency_[k], 0.0, error_values_by_year_[year][k - age_offset], year);
          // Deal with the plus group
          if (((k - age_offset + min_age_) >= max_age_) && plus_group_)
            plus_group += accumulated_age_frequency_[k];
          else if (((k - age_offset + min_age_) == max_age_) && !plus_group_)
            plus_group = accumulated_age_frequency_[k]; // no plus group and we are max age

        }
        SaveComparison(max_age_, 0, 0.0,  biomass, cell, plus_group, 0.0, error_values_by_year_[year][max_age_ - min_age_], year); // In this observation, I am replacing Error with biomass
      }
    }
    // Convert to propotions before simulating for each year and cell sum = 1
/*    for (auto& iter : comparisons_) {  // year
      for (auto& second_iter : iter.second) {  // cell
        float total_expec = 0.0;
        for (auto& comparison : second_iter.second)
          total_expec += comparison.expected_;
        for (auto& comparison : second_iter.second)
          comparison.expected_ /= total_expec;
      }
    }*/
    first_simualtion_run_ = false;
  }
  float total = 0.0;
  vector<float> total_by_cell(cells_.size() * years_.size(), 0.0);
  unsigned counter = 0;
  for (auto& iter : comparisons_) { // cell
    for (auto& second_iter : iter.second) {  // year
      total = 0.0;
      for (auto& comparison : second_iter.second) {
        total += comparison.expected_;
      }
      total_by_cell[counter] = total;
      ++counter;
      for (auto& comparison : second_iter.second)
        comparison.expected_ /= total;
    }
  }

  likelihood_->SimulateObserved(comparisons_);
  // Simualte numbers at age, but we want proportion
  counter = 0;
  for (auto& iter : comparisons_) {
    for (auto& second_iter : iter.second) {  // cell
      total = 0.0;
      for (auto& comparison : second_iter.second)
        total += comparison.simulated_;
      if (are_obs_props_ & ((likelihood_->type() == PARAM_MULTINOMIAL) | (likelihood_->type() == PARAM_DIRICHLET))) {
        for (auto& comparison : second_iter.second)
          comparison.simulated_ /= total;
      }
      if (not are_obs_props_& (likelihood_->type() == PARAM_LOGISTIC_NORMAL)) {
        for (auto& comparison : second_iter.second)
          comparison.simulated_ *=  total_by_cell[counter];
      }
      ++counter;
    }
  }
}

} /* namespace observations */
} /* namespace niwa */
