/**
 * @file MortalityBaranov.cpp
 * @author C.Marsh
 * @github https://github.com/Craig44
 * @date 21/09/2018
 * @section LICENSE
 *
 *
 * @section DESCRIPTION
 *
 */

// headers
#include "MortalityBaranov.h"

#include "Layers/Manager.h"
#include "Selectivities/Manager.h"
#include "Utilities/RandomNumberGenerator.h"
#include "World/WorldCell.h"
#include "World/WorldView.h"

// namespaces
namespace niwa {
namespace processes {

/**
 * constructor
 */
MortalityBaranov::MortalityBaranov(Model* model) : Mortality(model) {
  // F information
  parameters_.Bind<string>(PARAM_FISHERY_SELECTIVITY, &fishery_selectivity_label_, "Selectivity label for the fishery process", "");
  parameters_.Bind<unsigned>(PARAM_YEARS, &years_, "years to apply the fishery process in", "");
  parameters_.Bind<string>(PARAM_FISHING_MORTALITY_LAYERS, &f_layer_label_, "Spatial layer describing catch by cell for each year, there is a one to one link with the year specified, so make sure the order is right", "");
  parameters_.Bind<float>(PARAM_MINIMUM_LEGAL_LENGTH, &mls_, "The minimum legal length for this fishery, any individual less than this will be returned using some discard mortality", "", 0);
  parameters_.Bind<float>(PARAM_HANDLING_MORTALITY, &discard_mortality_, "if discarded due to being under the minimum legal length, what is the probability the individual will die when released", "", 0.0);
  // Sampling of the F process TODO add sampling coverage for observation error

  // Tagging information TODO if the fishery release or recaptures tagged fish add this functionality

  // M information
  parameters_.Bind<string>(PARAM_DISTRIBUTION, &distribution_, "the distribution to allocate the natural mortality parameter to the agents", "");
  parameters_.Bind<float>(PARAM_CV, &cv_, "The cv of the distribution for the M distribution", "");
  parameters_.Bind<string>(PARAM_M_MULTIPLIER_LAYER_LABEL, &m_layer_label_, "Label for the numeric layer that describes a multiplier of M through space", "", ""); // TODO perhaps as a multiplier, 1.2 * 0.2 = 0.24
  parameters_.Bind<bool>(PARAM_UPDATE_MORTALITY_PARAMETERS, &update_natural_mortality_parameters_, "If an agent/individual moves do you want it to take on the natural mortality parameters of the new spatial cell", "");
  parameters_.Bind<string>(PARAM_NATURAL_MORTALITY_SELECTIVITY, &natural_mortality_selectivity_label_, "Selectivity label for the natural mortality process", "");


}

/**
 * Do some initial checks of user supplied parameters.
 */
void MortalityBaranov::DoValidate() {
  LOG_TRACE();
  if (years_.size() != f_layer_label_.size())
    LOG_ERROR_P(PARAM_YEARS) << "you must specify a layer label for each year. You have supplied '" << years_.size() << "' years but '" << f_layer_label_.size() << "' fishing mortality labels, please sort this out.";
}

/**
 * DoBuild
 */
void MortalityBaranov::DoBuild() {
  LOG_FINE();

  /*
   *  Build M information
   */
  if (m_layer_label_ != "") {
    m_layer_ = model_->managers().layer()->GetNumericLayer(m_layer_label_);
    if (!m_layer_) {
      LOG_ERROR_P(PARAM_M_MULTIPLIER_LAYER_LABEL) << "could not find the layer '" << m_layer_label_ << "', please make sure it exists and that it is type 'numeric'";
    }
    // Do the multiplication
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      for (unsigned col = 0; col < model_->get_width(); ++col) {
        float multiplier = m_layer_->get_value(row, col);
        LOG_FINEST() << "multiplier = " << multiplier << " m value = " << m_;
        m_layer_->set_value(row, col, multiplier * m_);
        LOG_FINEST() << "check we set the right value = " << m_layer_->get_value(row, col);
      }
    }
  }
  LOG_FINEST() << "selectivities supplied = " << natural_mortality_selectivity_label_.size();
  // Build selectivity links
  if (natural_mortality_selectivity_label_.size() == 1)
    natural_mortality_selectivity_label_.assign(2, natural_mortality_selectivity_label_[0]);

  if (natural_mortality_selectivity_label_.size() > 2) {
    LOG_ERROR_P(PARAM_NATURAL_MORTALITY_SELECTIVITY) << "You suppled " << natural_mortality_selectivity_label_.size()  << " Selectiviites, you can only have one for each sex max = 2";
  }
  LOG_FINEST() << "selectivities supplied = " << natural_mortality_selectivity_label_.size();
  LOG_FINE() << "creating " << natural_mortality_selectivity_label_.size() << " selectivities";

  bool first = true;
  for (auto label : natural_mortality_selectivity_label_) {
    Selectivity* temp_selectivity = model_->managers().selectivity()->GetSelectivity(label);
    if (!temp_selectivity)
      LOG_ERROR_P(PARAM_NATURAL_MORTALITY_SELECTIVITY) << ": selectivity " << label << " does not exist. Have you defined it?";

    natural_mortality_selectivity_.push_back(temp_selectivity);
    if (first) {
      first = false;
      m_selectivity_length_based_ = temp_selectivity->is_length_based();
    } else {
      if (m_selectivity_length_based_ != temp_selectivity->is_length_based()) {
        LOG_ERROR_P(PARAM_NATURAL_MORTALITY_SELECTIVITY) << "The selectivity  " << label << " was not the same type (age or length based) as the previous selectivity label, they must be the same";
      }
    }
  }
  model_->set_m(m_);

  /*
   *  Build F information
   */
  for (auto& label : f_layer_label_) {
    layers::NumericLayer* temp_layer = nullptr;
    temp_layer = model_->managers().layer()->GetNumericLayer(label);
    if (!temp_layer) {
      LOG_FATAL_P(PARAM_FISHING_MORTALITY_LAYERS) << "could not find the layer '" << label << "', please make sure it exists and that it is type 'numeric'";
    }
    f_layer_.push_back(temp_layer);
  }

  LOG_FINEST() << "selectivities supplied = " << fishery_selectivity_label_.size();
  // Build selectivity links
  if (fishery_selectivity_label_.size() == 1)
    fishery_selectivity_label_.assign(2, fishery_selectivity_label_[0]);

  if (fishery_selectivity_label_.size() > 2) {
    LOG_ERROR_P(PARAM_FISHERY_SELECTIVITY) << "You suppled " << fishery_selectivity_label_.size()  << " Selectiviites, you can only have one for each sex max = 2";
  }
  LOG_FINEST() << "selectivities supplied = " << fishery_selectivity_label_.size();

  first = true;
  for (auto label : fishery_selectivity_label_) {
    Selectivity* temp_selectivity = model_->managers().selectivity()->GetSelectivity(label);
    if (!temp_selectivity)
      LOG_ERROR_P(PARAM_FISHERY_SELECTIVITY) << ": selectivity " << label << " does not exist. Have you defined it?";

    fishery_selectivity_.push_back(temp_selectivity);
    if (first) {
      first = false;
      f_selectivity_length_based_ = temp_selectivity->is_length_based();
    } else {
      if (f_selectivity_length_based_ != temp_selectivity->is_length_based()) {
        LOG_ERROR_P(PARAM_FISHERY_SELECTIVITY) << "The selectivity  " << label << " was not the same type (age or length based) as the previous selectivity label or the F selectivity";
      }
    }
  }
}

/**
 * DoExecute
 */
void MortalityBaranov::DoExecute() {
  LOG_MEDIUM();
  utilities::RandomNumberGenerator& rng = utilities::RandomNumberGenerator::Instance();

  // Check if we are in initialsiation or in an execute model year that we don't apply F then just apply M.
  if ((model_->state() == State::kInitialise) || (find(years_.begin(), years_.end(), model_->current_year()) != years_.end())) {
    LOG_FINEST() << "Just applying M";
    if (m_selectivity_length_based_) {
      LOG_FINE() << "M is length based";
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          WorldCell* cell = world_->get_base_square(row, col);
          if (cell->is_enabled()) {
            unsigned counter = 0;
            unsigned initial_size = cell->agents_.size();
            LOG_FINEST() << initial_size << " initial agents";
            for (auto iter = cell->agents_.begin(); iter != cell->agents_.end(); ++counter) {
              //LOG_FINEST() << "selectivity = " << selectivity_at_age << " m = " << (*iter).get_m();
              if (rng.chance() <= (1 - std::exp(- (*iter).get_m() * natural_mortality_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_length_bin_index())))) {
                iter = cell->agents_.erase(iter);
                initial_size--;
              } else
                ++iter;
            }
            LOG_FINEST() << initial_size << " after mortality";
          }
        }
      }
    } else {
      LOG_FINE() << "M is age based";
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          WorldCell* cell = world_->get_base_square(row, col);
          if (cell->is_enabled()) {
            unsigned counter = 0;
            unsigned initial_size = cell->agents_.size();
            LOG_FINEST() << initial_size << " initial agents";
            for (auto iter = cell->agents_.begin(); iter != cell->agents_.end(); ++counter) {
              if (rng.chance() <= (1 - std::exp(- (*iter).get_m() * natural_mortality_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_age_index())))) {
                iter = cell->agents_.erase(iter);
                initial_size--;
              } else
                ++iter;
            }
            LOG_FINEST() << initial_size << " after mortality";
          }
        }
      }
    }
  } else {
    LOG_FINE() << "applying simulaneous M + F";
    auto year_iter = find(years_.begin(), years_.end(), model_->current_year());
    unsigned year_ndx = distance(years_.begin(), year_iter);
    vector<unsigned> global_age_freq(model_->age_spread(), 0);
    float total_catch = 0;
    // There are 4 combos that we will split out M age - F age, M age - F length, M-length F age, M length - F length
    if (m_selectivity_length_based_ & f_selectivity_length_based_) {
      LOG_FINE() << "both M and F are length based";
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          WorldCell* cell = world_->get_base_square(row, col);
          if (cell->is_enabled()) {
            composition_data age_freq(PARAM_AGE, model_->current_year(), row, col, model_->age_spread());
            composition_data length_freq(PARAM_LENGTH, model_->current_year(), row, col, model_->length_bins().size());
            float catch_by_cell = 0;
            unsigned counter = 0;
            unsigned initial_size = cell->agents_.size();
            LOG_FINEST() << initial_size << " initial agents";
            float f_in_this_cell = f_layer_[year_ndx]->get_value(row, col);
            LOG_FINEST() << "We are fishing in cell " << row + 1 << " " << col + 1 << " value = " << f_in_this_cell;

            float f_l = 0;
            float m_l = 0;
            for (auto iter = cell->agents_.begin(); iter != cell->agents_.end(); ++counter) {
              m_l = (*iter).get_m() * natural_mortality_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_length_bin_index());
              f_l = f_in_this_cell * fishery_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_length_bin_index());
              if (rng.chance() <= (1 - std::exp(- (m_l + f_l)))) {
                // Remove this individual but first see if it is a M or F process
                if (rng.chance() < (f_l / (f_l + m_l))) {
                  // this individual dies because of F process
                  catch_by_cell += (*iter).get_weight() * (*iter).get_scalar();
                  total_catch += (*iter).get_weight() * (*iter).get_scalar();
                  age_freq.frequency_[(*iter).get_age_index()]++; //TODO do we need to multiple this by scalar for true numbers
                  length_freq.frequency_[(*iter).get_length_bin_index()]++;
                } // else it dies from M and we don't care about reporting that
                iter = cell->agents_.erase(iter);
                initial_size--;
              } else
                ++iter;
            }
            for (unsigned i = 0; i < model_->age_spread(); ++i)
              global_age_freq[i] += age_freq.frequency_[i];
            removals_by_length_and_area_.push_back(length_freq);
            removals_by_age_and_area_.push_back(age_freq);
            LOG_FINEST() << initial_size << " after mortality";
          }
        }
      }
    } else if (!m_selectivity_length_based_ & f_selectivity_length_based_) {
      LOG_FINE() << "M age based and F length based";
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          WorldCell* cell = world_->get_base_square(row, col);
          if (cell->is_enabled()) {
            composition_data age_freq(PARAM_AGE, model_->current_year(), row, col, model_->age_spread());
            composition_data length_freq(PARAM_LENGTH, model_->current_year(), row, col, model_->length_bins().size());
            float catch_by_cell = 0;
            unsigned counter = 0;
            unsigned initial_size = cell->agents_.size();
            LOG_FINEST() << initial_size << " initial agents";
            float f_in_this_cell = f_layer_[year_ndx]->get_value(row, col);
            LOG_FINEST() << "We are fishing in cell " << row + 1 << " " << col + 1 << " value = " << f_in_this_cell;

            float f_l = 0;
            float m_a = 0;
            for (auto iter = cell->agents_.begin(); iter != cell->agents_.end(); ++counter) {
              m_a = (*iter).get_m() * natural_mortality_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_age_index());
              f_l = f_in_this_cell * fishery_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_length_bin_index());
              if (rng.chance() <= (1 - std::exp(- (m_a + f_l)))) {
                // Remove this individual but first see if it is a M or F process
                if (rng.chance() < (f_l / (f_l + m_a))) {
                  // this individual dies because of F process
                  catch_by_cell += (*iter).get_weight() * (*iter).get_scalar();
                  total_catch += (*iter).get_weight() * (*iter).get_scalar();
                  age_freq.frequency_[(*iter).get_age_index()]++; //TODO do we need to multiple this by scalar for true numbers
                  length_freq.frequency_[(*iter).get_length_bin_index()]++;
                } // else it dies from M and we don't care about reporting that
                iter = cell->agents_.erase(iter);
                initial_size--;
              } else
                ++iter;
            }
            for (unsigned i = 0; i < model_->age_spread(); ++i)
              global_age_freq[i] += age_freq.frequency_[i];
            removals_by_length_and_area_.push_back(length_freq);
            removals_by_age_and_area_.push_back(age_freq);
            LOG_FINEST() << initial_size << " after mortality";
          }
        }
      }
    } else if (m_selectivity_length_based_ & !f_selectivity_length_based_) {
      LOG_FINE() << "M length based and F age based";
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          WorldCell* cell = world_->get_base_square(row, col);
          if (cell->is_enabled()) {
            composition_data age_freq(PARAM_AGE, model_->current_year(), row, col, model_->age_spread());
            composition_data length_freq(PARAM_LENGTH, model_->current_year(), row, col, model_->length_bins().size());
            float catch_by_cell = 0;
            unsigned counter = 0;
            unsigned initial_size = cell->agents_.size();
            LOG_FINEST() << initial_size << " initial agents";
            float f_in_this_cell = f_layer_[year_ndx]->get_value(row, col);
            LOG_FINEST() << "We are fishing in cell " << row + 1 << " " << col + 1 << " value = " << f_in_this_cell;

            float f_a = 0;
            float m_l = 0;
            for (auto iter = cell->agents_.begin(); iter != cell->agents_.end(); ++counter) {
              m_l = (*iter).get_m() * natural_mortality_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_length_bin_index());
              f_a = f_in_this_cell * fishery_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_age_index());
              if (rng.chance() <= (1 - std::exp(- (m_l + f_a)))) {
                // Remove this individual but first see if it is a M or F process
                if (rng.chance() < (f_a / (f_a + m_l))) {
                  // this individual dies because of F process
                  catch_by_cell += (*iter).get_weight() * (*iter).get_scalar();
                  total_catch += (*iter).get_weight() * (*iter).get_scalar();
                  age_freq.frequency_[(*iter).get_age_index()]++; //TODO do we need to multiple this by scalar for true numbers
                  length_freq.frequency_[(*iter).get_length_bin_index()]++;
                } // else it dies from M and we don't care about reporting that
                iter = cell->agents_.erase(iter);
                initial_size--;
              } else
                ++iter;
            }
            for (unsigned i = 0; i < model_->age_spread(); ++i)
              global_age_freq[i] += age_freq.frequency_[i];
            removals_by_length_and_area_.push_back(length_freq);
            removals_by_age_and_area_.push_back(age_freq);
            LOG_FINEST() << initial_size << " after mortality";
          }
        }
      }
    } else {
      LOG_FINE() << "M age based and F age based";
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          WorldCell* cell = world_->get_base_square(row, col);
          if (cell->is_enabled()) {
            composition_data age_freq(PARAM_AGE, model_->current_year(), row, col, model_->age_spread());
            composition_data length_freq(PARAM_LENGTH, model_->current_year(), row, col, model_->length_bins().size());
            float catch_by_cell = 0;
            unsigned counter = 0;
            unsigned initial_size = cell->agents_.size();
            LOG_FINEST() << initial_size << " initial agents";
            float f_in_this_cell = f_layer_[year_ndx]->get_value(row, col);
            LOG_FINEST() << "We are fishing in cell " << row + 1 << " " << col + 1 << " value = " << f_in_this_cell;

            float f_a = 0;
            float m_a = 0;
            for (auto iter = cell->agents_.begin(); iter != cell->agents_.end(); ++counter) {
              m_a = (*iter).get_m() * natural_mortality_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_age_index());
              f_a = f_in_this_cell * fishery_selectivity_[(*iter).get_sex()]->GetResult((*iter).get_age_index());
              if (rng.chance() <= (1 - std::exp(- (m_a + f_a)))) {
                // Remove this individual but first see if it is a M or F process
                if (rng.chance() < (f_a / (f_a + m_a))) {
                  // this individual dies because of F process
                  catch_by_cell += (*iter).get_weight() * (*iter).get_scalar();
                  total_catch += (*iter).get_weight() * (*iter).get_scalar();
                  age_freq.frequency_[(*iter).get_age_index()]++; //TODO do we need to multiple this by scalar for true numbers
                  length_freq.frequency_[(*iter).get_length_bin_index()]++;
                } // else it dies from M and we don't care about reporting that
                iter = cell->agents_.erase(iter);
                initial_size--;
              } else
                ++iter;
            }
            for (unsigned i = 0; i < model_->age_spread(); ++i)
              global_age_freq[i] += age_freq.frequency_[i];
            removals_by_length_and_area_.push_back(length_freq);
            removals_by_age_and_area_.push_back(age_freq);
            LOG_FINEST() << initial_size << " after mortality";
          }
        }
      }
    }
    catch_by_year_[model_->current_year()] = total_catch;
  }// M + F
}

/*
 * This method is called at when ever an agent is created/seeded or moves. Agents will get a new/updated growth parameters
 * based on the spatial cells of the process. This is called in initialisation/Recruitment and movement processes if needed.
*/
void  MortalityBaranov::draw_rate_param(unsigned row, unsigned col, unsigned number_of_draws, vector<float>& vector) {
  utilities::RandomNumberGenerator& rng = utilities::RandomNumberGenerator::Instance();
  float mean_m;
  if (m_layer_)
    mean_m = m_layer_->get_value(row, col) * m_;
  else
    mean_m = m_;

  LOG_FINE() << "mean M = " << mean_m;
  vector.clear();
  vector.resize(number_of_draws);

  //LOG_FINEST() << "mean M = " << mean_m;
  for (unsigned i = 0; i < number_of_draws; ++i) {
    float value = rng.lognormal(mean_m, cv_);
    //LOG_FINEST() << "value of M = " << value;
    vector[i] = value;
  }
}



// FillReportCache, called in the report class, it will print out additional information that is stored in
// containers in this class.
void  MortalityBaranov::FillReportCache(ostringstream& cache) {

  cache << "biomass_removed: ";
  for (auto& year : catch_by_year_)
    cache << year.second << " ";
  cache << "\ncatch_input_removed: ";
  for (auto& year : catch_by_year_)
    cache << year.second << " ";
  cache << "\n";

  if (removals_by_age_.size() > 0) {
    cache << "age_frequency " << REPORT_R_DATAFRAME << "\n";
    cache << "year ";
    for (unsigned i = model_->min_age(); i <= model_->max_age(); ++i)
      cache << i << " ";
    cache << "\n";
    for (auto& age_freq : removals_by_age_) {
      cache << age_freq.first << " ";
      for (auto age_value : age_freq.second)
        cache << age_value << " ";
      cache << "\n";
    }
  }

}

} /* namespace processes */
} /* namespace niwa */