/**
 * @file MovementPreference.cpp
 * @author C.Marsh
 * @github https://github.com/Craig44
 * @date 26/07/2018
 * @section LICENSE
 *
 *
 * @section DESCRIPTION
 *
 *
 */

// headers
#include "MovementPreference.h"

#include "Layers/Manager.h"
#include "PreferenceFunctions/Manager.h"

//#include "Selectivities/Manager.h"
#include "Utilities/RandomNumberGenerator.h"
#include "Utilities/To.h"

#include "World/WorldCell.h"
#include "World/WorldView.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/join.hpp>
#include <omp.h>
// namespaces
namespace niwa {
namespace processes {

/**
 *  constructor
 */
MovementPreference::MovementPreference(Model* model) : Movement(model) {
  process_type_ = ProcessType::kTransition;
  //parameters_.Bind<string>(PARAM_SELECTIVITY, &selectivity_label_, "Selectivity label", "");
  parameters_.Bind<float>(PARAM_D_MAX, &d_max_, "The maxiumu diffusion value", "")->set_lower_bound(0.0, false);
  parameters_.Bind<float>(PARAM_ZETA, &zeta_, "An arbiituary parameter that controls curvature between preference and diffusion", "", 1.0)->set_lower_bound(0.0, false);
  parameters_.Bind<bool>(PARAM_BROWNIAN_MOTION, &brownian_motion_, "Just apply random movement, undirected movement", "", false);
  parameters_.Bind<string>(PARAM_PREFERENCE_FUNCTIONS, &preference_function_labels_, "The preference functions to apply", "", true);
  parameters_.Bind<string>(PARAM_PREFERENCE_LAYERS, &preference_layer_labels_, "The preference functions to apply", "", true);
}

/*
 * DoValidate
*/
void MovementPreference::DoValidate() {
  LOG_TRACE();

}

/**
 *  Build relationships with other classes
 */
void MovementPreference::DoBuild() {
  LOG_FINE();
  // Get the preference functions
  for (auto& label : preference_function_labels_) {
    PreferenceFunction* temp_func = nullptr;
    temp_func = model_->managers().preference_function()->GetPreferenceFunction(label);
    if (!temp_func) {
      LOG_FATAL_P(PARAM_PREFERENCE_FUNCTIONS) << "could not find the preference function '" << label << "', please make sure it exists'";
    }
    preference_functions_.push_back(temp_func);
  }
  // Get the layers
  for (auto& label : preference_layer_labels_) {
    layers::NumericLayer* temp_layer = nullptr;
    temp_layer = model_->managers().layer()->GetNumericLayer(label);
    if (!temp_layer) {
      LOG_FATAL_P(PARAM_PREFERENCE_LAYERS) << "could not find the layer '" << label << "', please make sure it exists, and if it does exist make sure it is of type 'numeric''";
    }
    preference_layers_.push_back(temp_layer);
  }

  calculate_gradients();

  // check that lat and long layers have been supplied
  if (!model_->lat_and_long_supplied()) {
    LOG_ERROR_P(PARAM_LABEL) << "in order to apply the preference movement function, you need to specify lat and long bounds on the @model block";
  }

  cell_offset_.resize(model_->get_height());
  for (unsigned i = 0; i < model_->get_height(); ++i)
    cell_offset_[i].resize(model_->get_width());

}


/**
 *  Execute process
 */
void MovementPreference::DoExecute() {
  LOG_FINE();
  utilities::RandomNumberGenerator& rng = utilities::RandomNumberGenerator::Instance();

  // Pre-calculate agents in the world to set aside our random numbers needed for the operation
  n_agents_ = 0;
  for (unsigned row = 0; row < model_->get_height(); ++row) {
    for (unsigned col = 0; col < model_->get_width(); ++col) {
      WorldCell* cell = world_->get_base_square(row, col);
      if (cell->is_enabled()) {
        cell_offset_[row][col] = n_agents_;
        LOG_FINEST() << "row = " << row << " col = " << col << " offset = " << n_agents_ << " n-agents in this cell = " << cell->agents_.size();
        n_agents_ += cell->agents_.size();
      }
    }
  }
  // Allocate a single block of memory rather than each thread temporarily allocating their own memory.
  lon_random_numbers_.resize(n_agents_ + 1);
  lat_random_numbers_.resize(n_agents_ + 1);
  for (unsigned i = 0; i <= n_agents_; ++i) {
    lat_random_numbers_[i] = rng.normal();
    lon_random_numbers_[i] = rng.normal();
  }
  LOG_FINEST() << "random numbers generatored = " << lat_random_numbers_.size();

  // Iterate over origin cells
  #pragma omp parallel for collapse(2)
  for (unsigned row = 0; row < model_->get_height(); ++row) {
    for (unsigned col = 0; col < model_->get_width(); ++col) {
      LOG_FINEST() << "row = " << row << " col = " << col  << " thread id = " << omp_get_thread_num();
      WorldCell* origin_cell = world_->get_base_square(row, col);
      WorldCell* destination_cell;
      if (origin_cell->is_enabled()) {
        LOG_FINEST() << "number of agents in this cell = " << origin_cell->agents_.size();
        MovementData store_infor(model_->get_height(), model_->get_width(), origin_cell->get_cell_label(), model_->current_year());
        float u, v, lat_distance, lon_distance;
        unsigned destination_row, destination_col;
        // get gradients for current cells
        if (brownian_motion_) {
          u = 0;
          v = 0;
          standard_deviation_ = sqrt(2 * d_max_ * 1);  // TODO if you apply this many times, will need to change the temporal multiplier
        } else if (model_->state() == State::kInitialise) {
          v = initialisation_meridonal_gradient_[row][col];
          u = initialisation_zonal_gradient_[row][col];
          #pragma omp critical
          {
            calculate_diffusion_parameter(initialisation_preference_value_[row][col], standard_deviation_);
          }
        } else  {
          v = meridonal_gradient_[model_->current_year()][row][col];
          u = zonal_gradient_[model_->current_year()][row][col];
          #pragma omp critical
          {
            calculate_diffusion_parameter(preference_by_year_[model_->current_year()][row][col], standard_deviation_);
          }
        }

        LOG_FINE() << "n agents in this cell = " <<  origin_cell->agents_.size();
        store_infor.initial_numbers_ = origin_cell->agents_.size();

        unsigned counter = 0;
        for (auto iter = origin_cell->agents_.begin(); iter != origin_cell->agents_.end(); ++counter) {
          // Iterate over possible cells compare to chance()

          lat_distance = u + lat_random_numbers_[cell_offset_[row][col] + counter] * standard_deviation_;
          lon_distance = v + lon_random_numbers_[cell_offset_[row][col] + counter] * standard_deviation_;
          LOG_FINEST() << counter<< " " << (*iter).get_lat() << " distance = " << lat_distance << " lon = " << (*iter).get_lon() << " distance = " << lon_distance << " Z = " << lon_random_numbers_[cell_offset_[row][col] + counter] << " sigma = " << standard_deviation_;
          // Check bounds and find cell destination
          if (((*iter).get_lat() + lat_distance <= model_->max_lat()) && ((*iter).get_lat() - lat_distance >= model_->min_lat())) {
            (*iter).set_lat((*iter).get_lat() + lat_distance);
          } // else they stay as it would be jumping out of bounds

          if (((*iter).get_lon() + lon_distance <= model_->max_lon()) && ((*iter).get_lon() - lat_distance >= model_->min_lon())) {
            (*iter).set_lon((*iter).get_lon() + lon_distance);
          } // else they stay as it would be jumping out of bounds

          world_->get_cell_element(destination_row, destination_col, (*iter).get_lat(), (*iter).get_lon());

          LOG_FINEST() << (*iter).get_lat() << " " << (*iter).get_lon() << " " << destination_row << " " << destination_col << " " << row << " " << col;

          if (destination_row == row && destination_col == col) {
            store_infor.destination_of_agents_moved_[destination_row][destination_col]++;
            ++iter;
            continue;
          } else {
            destination_cell = world_->get_cached_square(destination_row, destination_col);
            if (destination_cell->is_enabled()) {
              // We are moving 'splice' this agent to the destination cache cell
              auto nx = next(iter); // Need to next the iter else we iter changes scope to cached agents, an annoying stl thing
              store_infor.destination_of_agents_moved_[destination_row][destination_col]++;
              #pragma omp critical
              {
                destination_cell->agents_.splice(destination_cell->agents_.end(), origin_cell->agents_, iter);
              }
              iter = nx;
            } else {
              ++iter;
            }
          }
        }
        if (model_->state() != State::kInitialise) {
          #pragma omp critical
          {
            moved_agents_by_year_.push_back(store_infor);
          }
        }
      }
    }
  }
  // merge destination agents into the actual grid
  LOG_FINE() << "Mergind cached world";
  world_->MergeCachedGrid();
  LOG_TRACE();
}

/*
 * Calculate gradient using the difference method, for each year and initialisation
*/
void  MovementPreference::calculate_gradients() {
  LOG_FINE();
  if (brownian_motion_)
    return void();
  // Start with setting the initialisation sets
  initialisation_meridonal_gradient_.resize(model_->get_height());
  initialisation_zonal_gradient_.resize(model_->get_height());
  vector<vector<float>> preference(model_->get_height());
  initialisation_preference_value_.resize(model_->get_height());
  for (unsigned row = 0; row < model_->get_height(); ++row) {
    initialisation_meridonal_gradient_[row].resize(model_->get_height());
    initialisation_zonal_gradient_[row].resize(model_->get_height(), 1.0);
    preference[row].resize(model_->get_height(), 1.0);
    initialisation_preference_value_[row].resize(model_->get_height(), 1.0);
  }

  for (unsigned layer_iter = 0; layer_iter < preference_layer_labels_.size(); ++layer_iter) {
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      for (unsigned col = 0; col < model_->get_width(); ++col) {
        preference[row][col] *= preference_functions_[layer_iter]->get_result(preference_layers_[layer_iter]->get_value(row, col, 0));  // the 0 in the layers call will return the default layer
      }
    }
  }
  // take the preference to the power
  for (unsigned row = 0; row < model_->get_height(); ++row) {
    for (unsigned col = 0; col < model_->get_width(); ++col) {
      initialisation_preference_value_[row][col] = pow(preference[row][col], (float)(1 / preference_layer_labels_.size()));
    }
  }
  // calculate gradient
  for (unsigned row = 0; row < model_->get_height(); ++row) {
    for (unsigned col = 0; col < model_->get_width(); ++col) {
      // calcualte meridional gradient
      if (row == 0) {
        initialisation_meridonal_gradient_[row][col] = initialisation_preference_value_[row + 1][col] - initialisation_preference_value_[row][col];
      } else if (row == (model_->get_height() - 1)) {
        initialisation_meridonal_gradient_[row][col] = initialisation_preference_value_[row][col] - initialisation_preference_value_[row - 1][col];
      } else {
        initialisation_meridonal_gradient_[row][col] = (initialisation_preference_value_[row + 1][col] - initialisation_preference_value_[row - 1][col]) / 2.0;
      }
      // calcualte zonal gradient
      if (col == 0) {
        initialisation_zonal_gradient_[row][col] = initialisation_preference_value_[row][col + 1] - initialisation_preference_value_[row][col];
      } else if (col == (model_->get_width() - 1)) {
        initialisation_zonal_gradient_[row][col] = initialisation_preference_value_[row][col] - initialisation_preference_value_[row][col - 1];
      } else {
        initialisation_zonal_gradient_[row][col] = (initialisation_preference_value_[row][col + 1] - initialisation_preference_value_[row][col - 1]) / 2.0;
      }
    }
  }
  // Now do it for all years
  for (auto year : model_->years()) {
    // initialise temporary containers
    vector<vector<float>> preference(model_->get_height());
    vector<vector<float>> zonal_gradient(model_->get_height());
    vector<vector<float>> meredional_gradient(model_->get_height());
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      preference[row].resize(model_->get_height());
      zonal_gradient[row].resize(model_->get_height(), 1.0);
      meredional_gradient[row].resize(model_->get_height(), 1.0);
    }

    for (unsigned layer_iter = 0; layer_iter < preference_layer_labels_.size(); ++layer_iter) {
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          preference[row][col] *= preference_functions_[layer_iter]->get_result(preference_layers_[layer_iter]->get_value(row, col, 0));  // the 0 in the layers call will return the default layer
        }
      }
    }
    preference_by_year_[year] = preference;
    // calculate gradient
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      for (unsigned col = 0; col < model_->get_width(); ++col) {
        // calcualte meridional gradient
        if (row == 0) {
          meredional_gradient[row][col] = preference[row + 1][col] - preference[row][col];
        } else if (row == (model_->get_height() - 1)) {
          meredional_gradient[row][col] = preference[row][col] - preference[row - 1][col];
        } else {
          meredional_gradient[row][col] = (preference[row + 1][col] - preference[row - 1][col]) / 2.0;
        }
        // calcualte zonal gradient
        if (col == 0) {
          zonal_gradient[row][col] = preference[row][col + 1] - preference[row][col];
        } else if (col == (model_->get_width() - 1)) {
          zonal_gradient[row][col] = preference[row][col] - preference[row][col - 1];
        } else {
          zonal_gradient[row][col] = (preference[row][col + 1] - preference[row][col - 1]) / 2.0;
        }
      }
    }
    meridonal_gradient_[year] = meredional_gradient;
    zonal_gradient_[year] = zonal_gradient;
  }
}

/*
 * This function just recalculates the random component of the random walk around the preference space, which depends on habitat quality
*/
void  MovementPreference::calculate_diffusion_parameter(float& preference_value, float& standard_deviation) {
  diffusion_parameter_ = d_max_ * (1 - (preference_value / (zeta_ + preference_value)));
  if (brownian_motion_)
    diffusion_parameter_ = d_max_;
  standard_deviation = sqrt(2 * diffusion_parameter_ * 1);  // TODO if you apply this many times, will need to change the temporal multiplier
}

// FillReportCache, called in the report class, it will print out additional information that is stored in
// containers in this class.
void  MovementPreference::FillReportCache(ostringstream& cache) {
  LOG_TRACE();
  for (auto& values : moved_agents_by_year_) {
    cache << "initial_numbers_in_cell: " << values.initial_numbers_ << "\n";
    cache << values.year_ << "_" << values.origin_cell_ << "_destination " << REPORT_R_MATRIX << "\n";
    for (unsigned i = 0; i < values.destination_of_agents_moved_.size(); ++i) {
      for (unsigned j = 0; j < values.destination_of_agents_moved_[i].size(); ++j )
        cache << values.destination_of_agents_moved_[i][j] << " ";
      cache << "\n";
    }
  }
}
} /* namespace processes */
} /* namespace niwa */
