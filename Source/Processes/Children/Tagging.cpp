/**
 * @file Tagging.cpp
 * @author C.Marsh
 * @github https://github.com/Craig44
 * @date 26/7/2018
 * @section LICENSE
 *
 *
 * @section DESCRIPTION
 *
 * This file exists to keep documentation generator happy
 */

// headers
#include "Tagging.h"

#include "Selectivities/Manager.h"
#include "Layers/Manager.h"
#include "Utilities/RandomNumberGenerator.h"
#include "World/WorldCell.h"
#include "World/WorldView.h"
#include "Agents/Agent.h"

// namespaces
namespace niwa {
namespace processes {

/**
 * Empty constructor
 */
Tagging::Tagging(Model* model) : Process(model) {
  parameters_.Bind<string>(PARAM_TAGGING_LAYERS, &tag_layer_label_, "Spatial layer describing catch by cell for each year, there is a one to one link with the year specified, so make sure the order is right", "", true);
  parameters_.Bind<string>(PARAM_SELECTIVITIES, &selectivity_labels_, "selectivity used to capture agents", "");
  parameters_.Bind<unsigned>(PARAM_YEARS, &years_, "Years to execute the transition in", "");
  process_type_ = ProcessType::kTransition;
}


void Tagging::DoValidate() {
  LOG_FINE();
  if (years_.size() != tag_layer_label_.size()) {
    LOG_ERROR_P(PARAM_YEARS) << "there needs to be a layer for all years to apply tagging in. You supplied " << years_.size() << " years but " << tag_layer_label_.size() << " labels, sort this descrepency out please";
  }

}

void Tagging::DoBuild() {
  LOG_FINE();
// Get the layers
  for (auto& label : tag_layer_label_) {
    layers::IntLayer* temp_layer = nullptr;
    temp_layer = model_->managers().layer()->GetIntLayer(label);
    if (!temp_layer) {
      LOG_FATAL_P(PARAM_TAGGING_LAYERS) << "could not find the layer '" << label << "', please make sure it exists and that it is type 'integer'";
    }
    tag_layer_.push_back(temp_layer);
  }

  // Build selectivities
  bool first = true;
  for (auto label : selectivity_labels_) {
    Selectivity* temp_selectivity = model_->managers().selectivity()->GetSelectivity(label);
    if (!temp_selectivity)
      LOG_ERROR_P(PARAM_SELECTIVITIES) << ": selectivity " << label << " does not exist. Have you defined it?";

    selectivities_.push_back(temp_selectivity);
    if (first) {
      first = false;
      selectivity_length_based_ = temp_selectivity->is_length_based();
    } else {
      if (selectivity_length_based_ != temp_selectivity->is_length_based()) {
        LOG_ERROR_P(PARAM_SELECTIVITIES) << "The selectivity  " << label << " was not the same type (age or length based) as the previous selectivity label";
      }
    }
  }
  // Build cell specific containers in the hope that we can thread
  cell_offset_for_selectivity_.resize(model_->get_height());
  cell_offset_.resize(model_->get_height());
  model_length_bins_.resize(model_->get_height());
  model_age_bins_.resize(model_->get_height());
  current_year_by_space_.resize(model_->get_height());
  for (unsigned i = 0; i < model_->get_height(); ++i) {
    cell_offset_[i].resize(model_->get_width());
    cell_offset_for_selectivity_[i].resize(model_->get_width());
    model_length_bins_[i].resize(model_->get_width(), model_->length_bins().size());
    model_age_bins_[i].resize(model_->get_width(), model_->age_spread());
    current_year_by_space_[i].resize(model_->get_width());
  }
  if (selectivity_length_based_) {
    for (unsigned i = 0; i < model_->get_height(); ++i) {
      for (unsigned j = 0; j < model_->get_width(); ++j) {
        for (unsigned ogive = 0; ogive < selectivity_labels_.size(); ++ogive) {
          for (auto len : model_->length_bins())
            cell_offset_for_selectivity_[i][j].push_back(selectivities_[ogive]->GetResult(len));
        }
      }
    }
  } else {
    for (unsigned i = 0; i < model_->get_height(); ++i) {
      for (unsigned j = 0; j < model_->get_width(); ++j) {
        for (unsigned ogive = 0; ogive < selectivity_labels_.size(); ++ogive) {
          for (auto age = model_->min_age(); age <= model_->max_age(); ++age)
          cell_offset_for_selectivity_[i][j].push_back(selectivities_[ogive]->GetResult(age));
        }
      }
    }
  }
}

void Tagging::DoExecute() {
  auto year_iter = years_.begin();
  if ((model_->state() != State::kInitialise) & (find(year_iter, years_.end(), model_->current_year()) != years_.end())) {
    utilities::RandomNumberGenerator& rng = utilities::RandomNumberGenerator::Instance();
    unsigned year_ndx = distance(years_.begin(), year_iter);
    // Are we applying Tagging either releases or scanning, or both
    LOG_MEDIUM();
    // Pre-calculate agents in the world to set aside our random numbers needed for the operation
    n_agents_ = 0;
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      for (unsigned col = 0; col < model_->get_width(); ++col) {
        WorldCell* cell = world_->get_base_square(row, col);
        if (cell->is_enabled()) {
          cell_offset_[row][col] = n_agents_;
          n_agents_ += cell->agents_.size();
          current_year_by_space_[row][col] = model_->current_year();
        }
      }
    }
    age_distribution_of_tagged_fish_by_year_[model_->current_year()].resize(model_->age_spread(),0);
    length_distribution_of_tagged_fish_by_year_[model_->current_year()].resize(model_->length_bins().size(),0);

    // Allocate a single block of memory rather than each thread temporarily allocating their own memory.
    random_numbers_.resize(n_agents_ + 1);
    selectivity_random_numbers_.resize(n_agents_ + 1);
    for (unsigned i = 0; i <= n_agents_; ++i) {
      random_numbers_[i] = rng.chance();
      selectivity_random_numbers_[i] = rng.chance();
    }
    LOG_FINE() << "about to apply tagging";
    if (not selectivity_length_based_) {
      // Thread out each loop
      #pragma omp parallel for collapse(2)
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          WorldCell* cell = nullptr;
          unsigned tags_to_release = 0;
          #pragma omp critical
          {
            cell = world_->get_base_square(row, col); // Shared resource...
            tags_to_release = tag_layer_[year_ndx]->get_value(row, col);
          }
          if (cell->is_enabled()) {
            unsigned tag_attempts = 1;
            auto iter = cell->agents_.begin();
            unsigned random_agent;
            unsigned counter = 0;
            unsigned tag_max = cell->agents_.size();
            vector<unsigned>  age_freq(model_age_bins_[row][col],0);
            vector<unsigned>  length_freq(model_length_bins_[row][col],0);
            LOG_FINE() << "row " << row + 1 << " col = " << col + 1 << " tags to release = " << tags_to_release;
            while (tags_to_release > 0) {
              ++tag_attempts;
              // pick a random agent
              iter = cell->agents_.begin();
              random_agent = random_numbers_[cell_offset_[row][col] + counter] * cell->agents_.size();
              advance(iter, random_agent);
              // See if it is vulnerable to selectivity
              if (selectivity_random_numbers_[cell_offset_[row][col] + counter] <= cell_offset_for_selectivity_[row][col][(*iter).get_sex() * model_age_bins_[row][col] + (*iter).get_age_index()]) {
                if ((*iter).get_number_tags() > 1) // This fish is alread tagged so pretend we didn't catch it
                  continue;

                // Caught this agent we need to split out a tagged fish and return the two types
                Agent tagged_agent(*iter);
                tagged_agent.apply_tagging_event(1); // Any tagging attribute should be bundled into this method
                (*iter).set_scalar((*iter).get_scalar() - 1.0);
                tagged_agent.set_scalar(1.0);
                age_freq[(*iter).get_age_index()]++;
                length_freq[(*iter).get_length_bin_index()]++;
                tags_to_release--;
                // add this tagged fish to the partition
                cell->agents_.push_back(tagged_agent);
              }
              // Make sure we don't end up fishing for infinity
              if (tag_attempts >= tag_max) {
                LOG_FATAL_P(PARAM_LABEL) << "Too many attempts to catch an agent in the process " << label_ << " in year " << current_year_by_space_[row][col] << " in row " << row + 1 << " and column " << col + 1 << " this most likely means you have" <<
                   " a model that suggests there should be more agents in this space than than the current agent dynamics are putting in this cell, check the user manual for tips to resolve this situation, agents in cell = " << tag_max << " attempts made = " << tag_attempts;
              }
              ++counter;
            }
            // Store global information
            #pragma omp critical
            {
              for (unsigned age = 0; age < model_->age_spread(); ++age)
                age_distribution_of_tagged_fish_by_year_[model_->current_year()][age] += age_freq[age];
              for (unsigned length_ndx = 0; length_ndx < model_->length_bins().size(); ++length_ndx)
                length_distribution_of_tagged_fish_by_year_[model_->current_year()][length_ndx]+= length_freq[length_ndx];
            }

          }
        }
      }
    } else {
      LOG_FINE() << "length based tagging";
    }

  }
}

// FillReportCache, called in the report class, it will print out additional information that is stored in
// containers in this class.
void  Tagging::FillReportCache(ostringstream& cache) {
  cache << "global_tag_release_age_distribution " << REPORT_R_DATAFRAME << "\n";
  cache << "year";
  for (unsigned age = 0; age < model_->age_spread(); ++age) {
    cache << " " << age + model_->min_age();
  }
  for (auto& year_value : age_distribution_of_tagged_fish_by_year_) {
    cache << "\n" << year_value.first;
    for (unsigned age_ndx = 0; age_ndx < year_value.second.size(); ++age_ndx)
      cache << " " << year_value.second[age_ndx];
  }
  cache << "\n";
  cache << "global_tag_release_length_distribution " << REPORT_R_DATAFRAME << "\n";
  cache << "year";
  for (unsigned length_ndx = 0; length_ndx < model_->length_bins().size(); ++length_ndx) {
    cache << " " << model_->length_bins()[length_ndx];
  }
  for (auto& year_value : length_distribution_of_tagged_fish_by_year_) {
    cache << "\n" << year_value.first;
    for (unsigned length_ndx = 0; length_ndx < year_value.second.size(); ++length_ndx)
      cache << " " << year_value.second[length_ndx];
  }
  cache << "\n";
}

}
}
