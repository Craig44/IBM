/**
 * @file MortalityEventBiomassScaledAgeFrequency.cpp
 * @author  C.Marsh github.com/Craig44
 * @date 4/11/2018
 * @section LICENSE
 *
 *
 */

// headers
#include "MortalityEventBiomassScaledAgeFrequency.h"


#include "Processes/Manager.h"
#include "Layers/Manager.h"
#include "AgeingErrors/Manager.h"

#include "World/WorldView.h"
#include "World/WorldCell.h"

#include <omp.h>

#include "Utilities/RandomNumberGenerator.h"
#include "Utilities/To.h"
#include "Utilities/Map.h"
#include "Utilities/DoubleCompare.h"

// namespaces
namespace niwa {
namespace observations {

namespace utils = niwa::utilities;

/**
 * Default constructor
 */
MortalityEventBiomassScaledAgeFrequency::MortalityEventBiomassScaledAgeFrequency(Model* model) : Observation(model) {
  sample_table_ = new parameters::Table(PARAM_SAMPLES);
  lf_sample_table_ = new parameters::Table(PARAM_PROPORTION_LF_SAMPLED);

  parameters_.BindTable(PARAM_SAMPLES, sample_table_, "Table of sample sizes used to generate age length key for each stratum and each year.", "", false);
  parameters_.BindTable(PARAM_PROPORTION_LF_SAMPLED, lf_sample_table_, "Table of proportion of agents sampled for length frequency, by year (row) and stratum (col)", "", false);

  parameters_.Bind<unsigned>(PARAM_YEARS, &years_, "The years of the observed values", "");
  parameters_.Bind<string>(PARAM_AGEING_ERROR, &ageing_error_label_, "Label of ageing error to use", "", PARAM_NONE);
  parameters_.Bind<string>(PARAM_PROCESS_LABEL, &process_label_, "Label of of removal process", "", "");
  parameters_.Bind<string>(PARAM_FISHERY_LABEL, &fishery_label_, "Label of of removal process", "");

  parameters_.Bind<string>(PARAM_AGEING_ALLOCATION_METHOD, &ageing_allocation_, "The method used to allocate aged individuals across the length distribution", "", PARAM_RANDOM)->set_allowed_values({PARAM_RANDOM,PARAM_EQUAL,PARAM_PROPORTIONAL});
  // TODO add these in at some point ...
  //parameters_.Bind<unsigned>(PARAM_NUMBER_OF_BOOTSTRAPS, &number_of_bootstraps_, "Number of bootstraps to conduct for each stratum to calculate Pooled CV's for each stratum and total age frequency", "", 50);
  //parameters_.Bind<string>(PARAM_STRATUM_WEIGHT_METHOD, &stratum_weight_method_, "Method to weight stratum estimates by", "", PARAM_BIOMASS)->set_allowed_values({PARAM_BIOMASS, PARAM_AREA, PARAM_NONE});
  parameters_.Bind<string>(PARAM_SEX, &sexed_, "You can ask to 'ignore' sex (only option for unsexed model), or generate composition for a particular sex, either 'male' or 'female", "", PARAM_IGNORE)->set_allowed_values({PARAM_MALE,PARAM_FEMALE,PARAM_IGNORE});

  parameters_.Bind<string>(PARAM_LAYER_OF_STRATUM_DEFINITIONS, &layer_label_, "The layer that indicates what the stratum boundaries are.", "");
  parameters_.Bind<string>(PARAM_STRATUMS_TO_INCLUDE, &cells_, "The cells which represent individual stratum to be included in the analysis, default is all cells are used from the layer", "", true);

}
/**
 * Destructor
 */
MortalityEventBiomassScaledAgeFrequency::~MortalityEventBiomassScaledAgeFrequency() {
  delete sample_table_;
  delete lf_sample_table_;
}
/**
 *
 */
void MortalityEventBiomassScaledAgeFrequency::DoValidate() {
  LOG_TRACE();
  for (auto year : years_) {
    LOG_FINE() << "year : " << year;
    if ((year < model_->start_year()) || (year > model_->final_year()))
      LOG_ERROR_P(PARAM_YEARS) << "Years can't be less than start_year (" << model_->start_year() << "), or greater than final_year (" << model_->final_year()
          << "). Please fix this.";
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

}

/**
 *
 */
void MortalityEventBiomassScaledAgeFrequency::DoBuild() {
  LOG_TRACE();
  // Create a pointer to misclassification matrix
  if (ageing_error_label_ != PARAM_NONE) {
    ageing_error_ = model_->managers().ageing_error()->GetAgeingError(ageing_error_label_);
    if (!ageing_error_)
      LOG_ERROR_P(PARAM_AGEING_ERROR) << "(" << ageing_error_label_ << ") could not be found. Have you defined it?";
  }
  if (ageing_error_label_ == PARAM_NONE) {
    LOG_WARNING() << "You are suppling an age based observation with no ageing misclassification error";
  }

  world_ = model_->world_view();
  if (!world_)
    LOG_CODE_ERROR() << "!world_ could not create pointer to world viw model, something is wrong";

  mortality_process_ = model_->managers().process()->GetMortalityEventBiomassProcess(process_label_);
  if (!mortality_process_)
    LOG_FATAL_P(PARAM_PROCESS_LABEL)<< "could not find the process " << process_label_ << ", please make sure it exists and is of type " << PARAM_MORTALITY_EVENT_BIOMASS;


    // Build and validate layers
  layer_ = model_->managers().layer()->GetCategoricalLayer(layer_label_);
  if (!layer_)
    LOG_FATAL_P(PARAM_LAYER_OF_STRATUM_DEFINITIONS)<< "could not find layer " << layer_label_ << " does it exist?, if it exists is of type categorical?";

  LOG_FINE() << "Check stratum are consistent";
  if (parameters_.Get(PARAM_STRATUMS_TO_INCLUDE)->has_been_defined()) {
    // Check all the cells supplied are in the layer
    for (auto cell : cells_) {
      LOG_FINE() << "checking cell " << cell << " exists in layer";
      stratum_area_[cell] = 0.0;
      bool cell_found = false;
      for (unsigned row = 0; row < model_->get_height(); ++row) {
        for (unsigned col = 0; col < model_->get_width(); ++col) {
          LOG_FINE() << "checking row = " << row << " col = " << col;
          LOG_FINE() << "value = " << layer_->get_value(row, col);
          if (layer_->get_value(row, col) == cell) {
            cell_found = true;
            stratum_rows_[cell].push_back(row);
            stratum_cols_[cell].push_back(col);

            if (stratum_weight_method_ == PARAM_AREA) {
              WorldCell* world_cell = world_->get_base_square(row, col);
              if (world_cell->is_enabled())
                stratum_area_[cell] = world_cell->get_area();
            }
          }
        }
      }
      if (not cell_found)
        LOG_ERROR_P(PARAM_STRATUMS_TO_INCLUDE) << "could not find the cell '" << cell << "' in the layer " << layer_label_
            << " please make sure that you supply cell labels that are consistent with the layer.";
    }
  } else {
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      for (unsigned col = 0; col < model_->get_width(); ++col) {
        string temp_cell = layer_->get_value(row, col);
        stratum_rows_[temp_cell].push_back(row);
        stratum_cols_[temp_cell].push_back(col);
        if (find(cells_.begin(), cells_.end(), temp_cell) == cells_.end())
          cells_.push_back(temp_cell);
        if (stratum_weight_method_ == PARAM_AREA) {
          WorldCell* world_cell = world_->get_base_square(row, col);
          if (world_cell->is_enabled())
            stratum_area_[temp_cell] = world_cell->get_area();
        }
      }
    }
  }
  LOG_FINE() << "Check mortality process is consistent with observation";
  if (not mortality_process_->check_years(years_)) {
    LOG_ERROR_P(PARAM_YEARS)
        << "there was a year that the mortality process doesn't not execute in, can you please check that the years you have supplied for this observation are years that the mortality process occurs in cheers.";
  }

  if (not mortality_process_->check_fishery_exists(fishery_label_)) {
    LOG_FATAL_P(PARAM_FISHERY_LABEL)
        << "could not find the fishery label " << fishery_label_ << " in the mortality process " << process_label_ << " please check it exists and or is spelt correctly";
  }

  fishery_years_ = mortality_process_->get_fishery_years();

  for (auto& row_map : stratum_rows_) {
    LOG_FINE() << "rows in cell " << row_map.first;
    for (auto val : row_map.second)
      LOG_FINE() << val;
  }
  for (auto& col_map : stratum_cols_) {
    LOG_FINE() << "cols in cell " << col_map.first;
    for (auto val : col_map.second)
      LOG_FINE() << val;
  }
  /*
   * Build sample table
  */
  LOG_FINE() << "Build sample table";
  vector<vector<string>>& sample_data = sample_table_->data();
  if ((sample_data.size() - 1) != years_.size()) {
    LOG_ERROR_P(PARAM_SAMPLES) << " has " << (sample_data.size() - 1) << " rows defined, but we expected " << years_.size()
        << " to match the number of years provided";
  }
  vector<string>  stratum_order_from_sample_table = sample_data[0];

  if (stratum_order_from_sample_table[0] != PARAM_YEAR)
    LOG_FATAL_P(PARAM_SAMPLES) << "Expected the first column to have the following header '" << PARAM_YEAR << "'";

  for (unsigned col_ndx = 1; col_ndx < stratum_order_from_sample_table.size(); ++col_ndx) {
    if (find(cells_.begin(),cells_.end(),stratum_order_from_sample_table[col_ndx]) == cells_.end())
      LOG_FATAL_P(PARAM_SAMPLES) << "Could not find the stratum '" << stratum_order_from_sample_table[col_ndx] << "' (colum header '" << col_ndx + 1 << "') in the parameter " << PARAM_STRATUMS_TO_INCLUDE << " can you please check that the column headers are consistent with this parameter, chairs";

  }



  for (unsigned row_counter = 1; row_counter < sample_data.size();++row_counter) {
    if (sample_data[row_counter].size() != (cells_.size() + 1)) {
      LOG_FATAL_P(PARAM_SAMPLES) << " has " << sample_data[row_counter].size() << " values defined, but we expected " << cells_.size() + 1
          << " to match a number for each stratum";
    }
    unsigned year = 0;
    if (!utilities::To<unsigned>(sample_data[row_counter][0], year))
      LOG_ERROR_P(PARAM_SAMPLES) << " value " << sample_data[row_counter][0] << " could not be converted in to an unsigned integer. It should be the year for this line";
    if (std::find(years_.begin(), years_.end(), year) == years_.end())
      LOG_ERROR_P(PARAM_SAMPLES) << " value " << year << " is not a valid year for this observation";

    LOG_FINE() << "Year = " << year;
    for (unsigned i = 1; i < sample_data[row_counter].size(); ++i) {
      unsigned value = 0;

      if (!utilities::To<unsigned>(sample_data[row_counter][i], value))
        LOG_FATAL_P(PARAM_SAMPLES) << " value (" << sample_data[row_counter][i] << ") could not be converted to a unsigned";
      if (value <= 0) {
        LOG_ERROR_P(PARAM_SAMPLES) << "at row = " << row_counter << " and column " << i << " the value given = " << value << " this needs to be a positive integer.";
      }
      LOG_FINE() << "stratum = " << stratum_order_from_sample_table[i] << " samples = " << value;

      samples_by_year_and_stratum_[year][stratum_order_from_sample_table[i]] = value;
    }
  }

  // Validate stratum lf proportions table
  vector<vector<string>>& prop_lf_data = lf_sample_table_->data();
  if ((prop_lf_data.size() - 1) != years_.size()) {
    LOG_ERROR_P(PARAM_PROPORTION_LF_SAMPLED) << " has " << (prop_lf_data.size() - 1) << " rows defined, but we expected " << years_.size()
        << " to match the number of years provided";
  }
  vector<string>  stratum_order_from_lf_table = prop_lf_data[0];

  if (stratum_order_from_lf_table[0] != PARAM_YEAR)
    LOG_FATAL_P(PARAM_PROPORTION_LF_SAMPLED) << "Expected the first column to have the following header '" << PARAM_YEAR << "'";

  for (unsigned col_ndx = 1; col_ndx < stratum_order_from_lf_table.size(); ++col_ndx) {
    if (find(cells_.begin(),cells_.end(),stratum_order_from_lf_table[col_ndx]) == cells_.end())
      LOG_FATAL_P(PARAM_PROPORTION_LF_SAMPLED) << "Could not find the stratum '" << stratum_order_from_lf_table[col_ndx] << "' (colum header '" << col_ndx + 1 << "') in the parameter " << PARAM_STRATUMS_TO_INCLUDE << " can you please check that the column headers are consistent with this parameter, chairs";
  }

  LOG_FINE() << "rows in LF table = " << prop_lf_data.size();
  for (unsigned row_counter = 1; row_counter < prop_lf_data.size(); ++row_counter) {
    if (prop_lf_data[row_counter].size() != (cells_.size() + 1)) {
      LOG_FATAL_P(PARAM_PROPORTION_LF_SAMPLED) << " has " << prop_lf_data[row_counter].size() << " values defined, but we expected " << cells_.size() + 1
          << " to match a number for each stratum";
    }
    unsigned year = 0;
    if (!utilities::To<unsigned>(prop_lf_data[row_counter][0], year))
      LOG_ERROR_P(PARAM_PROPORTION_LF_SAMPLED) << " value " << prop_lf_data[row_counter][0] << " could not be converted in to an unsigned integer. It should be the year for this line";
    if (std::find(years_.begin(), years_.end(), year) == years_.end())
      LOG_ERROR_P(PARAM_PROPORTION_LF_SAMPLED) << " value " << year << " is not a valid year for this observation";

    LOG_FINE() << "Year = " << year << " in table = " << prop_lf_data[row_counter][0];
    for (unsigned i = 1; i < prop_lf_data[row_counter].size(); ++i) {
      float value = 0;

      if (!utilities::To<float>(prop_lf_data[row_counter][i], value))
        LOG_FATAL_P(PARAM_PROPORTION_LF_SAMPLED) << " value (" << prop_lf_data[row_counter][i] << " at row = " << row_counter << " and column " << i << " could not be converted to a float";
      if ((value < 0) || (value > 1)) {
        LOG_ERROR_P(PARAM_PROPORTION_LF_SAMPLED) << "at row = " << row_counter << " and column " << i << " the value given = " << value << " this needs to be >= 0.0 and <= 1.0r.";
      }
      LOG_FINE() << "stratum = " << stratum_order_from_lf_table[i] << " samples = " << value;

      prop_lf_by_year_and_stratum_[year][stratum_order_from_lf_table[i]] = value;
    }
  }
  if (ageing_allocation_ == PARAM_EQUAL)
    allocation_type_ = AllocationType::kEqual;
  else if (ageing_allocation_ == PARAM_PROPORTIONAL)
    allocation_type_ = AllocationType::kProportional;

  // Allocate memory
  age_length_key_.resize(model_->age_spread());
  for (unsigned i = 0; i < model_->age_spread(); ++i)
    age_length_key_[i].resize(model_->number_of_length_bins(),0.0);

}

/**
 *
 */
void MortalityEventBiomassScaledAgeFrequency::PreExecute() {

}

/**
 *
 */
void MortalityEventBiomassScaledAgeFrequency::Execute() {

}

/**
 *
 */
void MortalityEventBiomassScaledAgeFrequency::Simulate() {
  LOG_MEDIUM() << "Simulating data for observation = " << label_;
  if (model_->run_mode() != (RunMode::Type)(RunMode::kMSE))
    ClearComparison(); // Clear comparisons

  utilities::RandomNumberGenerator& rng = utilities::RandomNumberGenerator::Instance();
  vector<vector<processes::census_data>> fishery_age_data = mortality_process_->get_fishery_census_data(fishery_label_);

  if (fishery_age_data.size() == 0) {
    LOG_CODE_ERROR() << "could not return information for fishery " << fishery_label_ << " this error should be dealt with earlier in the code";
  }

  //vector<processes::composition_data>& length_frequency = mortality_process_->get_removals_by_length();
  LOG_FINE() << "length of census data (years) = " << fishery_age_data.size();

  vector<unsigned> census_stratum_ndx;
  bool apply_ageing_error = true;
  if (!ageing_error_) {
    apply_ageing_error = false;
  }
  vector<unsigned> sim_years;
  if (model_->run_mode() == (RunMode::Type)(RunMode::kMSE))
    sim_years = model_->simulation_years();
  for (unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    // only do this in MSE mode if we are updateing
    if (model_->run_mode() == (RunMode::Type)(RunMode::kMSE)) {
      if(find(sim_years.begin(), sim_years.end(), years_[year_ndx]) == sim_years.end()) {
        continue;
      }
    }
    // find equivalent fishery index
    auto iter = find(fishery_years_.begin(), fishery_years_.end(), years_[year_ndx]);
    unsigned fishery_year_ndx = distance(fishery_years_.begin(), iter);
    LOG_FINE() << "About to sort our info for year " << years_[year_ndx] << " fishery index " << fishery_year_ndx;

    vector<processes::census_data>& fishery_year_census = fishery_age_data[fishery_year_ndx];
    unsigned agents_available_to_sample = 0;
    for (unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      LOG_FINE() << "About to sort our info for stratum " << cells_[stratum_ndx];
      vector<float> stratum_length_frequency(model_->length_bin_mid_points().size(),0.0);
      float proportion_lf_sampled = prop_lf_by_year_and_stratum_[years_[year_ndx]][cells_[stratum_ndx]];
      if (proportion_lf_sampled <= 0)
        LOG_CODE_ERROR() << "asked for an observation but dont want any samples this is a simple code error that needs to be addressed, catch this error earlier";
      census_stratum_ndx.clear();
      // clear ALK
      for (unsigned i = 0; i < model_->age_spread(); ++i)
        fill(age_length_key_[i].begin(), age_length_key_[i].end(),0.0);

      stratum_biomass_[cells_[stratum_ndx]] = 0.0;

      // Find out how many agents are available to be used for an ALK in this stratum
      // -- loop over all cells that the fishery occured in (census data)
      // -- if that area and fishery belongs to this stratum then summarise some numbers for use later.
      //
      // Calculate Length frequency for the strata
      unsigned census_ndx = 0; // links back to the census
      LOG_FINE() << "number of cells in this year and fishery = " << fishery_year_census.size();
      vector<vector<unsigned>> agents_ndx_measured_for_length(fishery_year_census.size());
      for (processes::census_data& census : fishery_year_census) {
        // Find census elements that are in this stratum
        if ((find(stratum_rows_[cells_[stratum_ndx]].begin(),stratum_rows_[cells_[stratum_ndx]].end(), census.row_) != stratum_rows_[cells_[stratum_ndx]].end()) && (find(stratum_cols_[cells_[stratum_ndx]].begin(),stratum_cols_[cells_[stratum_ndx]].end(), census.col_) != stratum_cols_[cells_[stratum_ndx]].end())) {

          if (census.age_ndx_.size() > 0) {
            //LOG_FINE() << "found a census that year and cell work, agents in it = " << census.age_ndx_.size() << " proportion to take = " << proportion_lf_sampled;
            census_stratum_ndx.push_back(census_ndx);
            // Calculate the agents available in the first sampling unit
            LOG_FINE() << "agents in this cell = " << census.age_ndx_.size();
            for (unsigned agent_ndx = 0; agent_ndx < census.age_ndx_.size(); ++ agent_ndx) {
              // check if sex specific observation
              if (sexed_flag_) {
                if (census.sex_[agent_ndx] != sex_match_)
                  continue;
              }
              if(rng.chance() <= proportion_lf_sampled) {
                stratum_length_frequency[census.length_ndx_[agent_ndx]] += census.scalar_[agent_ndx];
                agents_available_to_sample++;
                agents_ndx_measured_for_length[census_ndx].push_back(agent_ndx);
              }
            }
            // TODO add a weight component so that we can more accurately do this rather than this approximation.
            if (stratum_weight_method_ == PARAM_BIOMASS)
              stratum_biomass_[cells_[stratum_ndx]] += census.biomass_ * proportion_lf_sampled; // Note this is an approximation census data doesn't have agent specific weight
          }
        }
        ++census_ndx;
      }
      if (agents_available_to_sample == 0) {
        LOG_WARNING() << "no agents to sample in year " << years_[year_ndx] << " so skipping this year";
        continue;
      }
      unsigned samples_to_take = samples_by_year_and_stratum_[years_[year_ndx]][cells_[stratum_ndx]];

      if (agents_available_to_sample <= samples_to_take) {
        samples_to_take = (unsigned)(agents_available_to_sample * 0.7);

        LOG_WARNING() << "in the observation " << label_ << " for year = " << years_[year_ndx] << " and stratum = " << cells_[stratum_ndx] << ", you said you wanted " << samples_to_take << " samples, there are " << agents_available_to_sample << " agents available to sample, I have taken set the sample size to "  << samples_to_take << " which 70% of the available agents.";
      }
      LOG_FINE() << "samples to take = " << samples_to_take << ", agents available in this stratum = " << agents_available_to_sample;
      // Some containers to make sure that we are doing sampling WITHOUT replacement
      // This will slow it down, but worth it I believe
      vector<unsigned> census_sampled;
      vector<unsigned> agents_sampled;
      census_ndx = 0;
      unsigned agent_ndx;
      unsigned age_ndx = 0;
      unsigned length_ndx = 0;

      // Generate an age-length-key for this stratum

      LOG_FINE() << "about to build Age length key";
      float total_agents_in_ALK = 0;
      vector<vector<float>> mis_matrix;
      if (apply_ageing_error)
        mis_matrix = ageing_error_->mis_matrix();
      // declare some containers used for second level of sub-sampling age with length frequency
      vector<unsigned> expected_numbers_of_lf(stratum_length_frequency.size(), 0);
      vector<unsigned> sampled_numbers_of_lf(stratum_length_frequency.size(), 0);

      float non_zero_length_bins = 0;
      // number of length bins with non-zero entry
      float tot = 0;
      for (auto& len : stratum_length_frequency) {
        if (len > 0)
          ++non_zero_length_bins;
        tot += len;
      }
      LOG_FINE() << "number in individuals in length distribution = " << tot;

      if (allocation_type_ == AllocationType::kEqual) {
        for (unsigned len_bin = 0; len_bin < stratum_length_frequency.size(); ++len_bin) {
          if (stratum_length_frequency[len_bin] > 0)
            expected_numbers_of_lf[len_bin] = (unsigned) (samples_to_take * (1/non_zero_length_bins));
        }
      } else if (allocation_type_ == AllocationType::kProportional) {
        for (unsigned len_bin = 0; len_bin < stratum_length_frequency.size(); ++len_bin) {
          if (stratum_length_frequency[len_bin] > 0)
            expected_numbers_of_lf[len_bin] = (unsigned) (samples_to_take * (stratum_length_frequency[len_bin]/tot));
        }
      }

      LOG_FINE() << "samples to take " << samples_to_take;
      vector<unsigned> this_strata_age_freq(model_->age_spread(), 0.0);

      unsigned attempt = 0, age_samples_taken = 0, sample_attempt= 0;
      unsigned max_attempts =  agents_available_to_sample * 10;
      bool use_systematic_to_finish_sub_selection = false;
      // ------------------------
      // The main while loop to
      // allocate ages
      // ------------------------
      while (age_samples_taken < samples_to_take) {
        ++attempt;
        if (attempt > max_attempts) {
          use_systematic_to_finish_sub_selection = true;
          LOG_WARNING() << "in the observation " << label_ << " for year = " << years_[year_ndx] << " and stratum = " << cells_[stratum_ndx] << ", we were trying to sample for too long to build an age-length key. This is set at 10 x "
              "samples available to take. samples to include = " << samples_to_take << " samples taken = " << total_agents_in_ALK << " using systematic methods to fill number of otoliths";
          break;
        }
        // (1) Randomly select a cell that a stratum belongs to
        census_ndx = census_stratum_ndx[rng.chance() * census_stratum_ndx.size()];
        processes::census_data& this_census = fishery_year_census[census_ndx];

        // no agents to sample from this cell try again
        if (this_census.age_ndx_.size() <= 0) {
          LOG_FINEST() << "No agents in this cell so try again";
          continue;
        }
        // (2) Randomly select an agent in that cell that was measured for length
        agent_ndx = agents_ndx_measured_for_length[census_ndx][agents_ndx_measured_for_length[census_ndx].size() * rng.chance()];
        LOG_FINEST() << "census index = " << census_ndx << " agent ndx = " << agent_ndx << " attempt = " << attempt;

        // (3) We are sampling without replacement so check we haven't sampled this agent.
        for (unsigned check_iter = 0; check_iter < census_sampled.size(); ++check_iter) {
          if ((agents_sampled[check_iter] == agent_ndx) & (census_sampled[check_iter] == census_ndx))
            LOG_FINEST() << "we have sampled this agent already, try again";
          continue;
        }
        length_ndx = this_census.length_ndx_[agent_ndx];

        // (4) apply allocation method
        if (allocation_type_ != AllocationType::kRandom) {
           if (sampled_numbers_of_lf[length_ndx] >= expected_numbers_of_lf[length_ndx]) {
             // We have selected the neccassary number of agents to age in this length bin try another fish
             // TODO this gets called alot, can we make this more effecient...
             LOG_FINEST() << " We have selected the neccassary number of agents to age in this length bin try another fish";
             continue;
           }
        }

        // Cooking with gas

        // (5) Are we applying ageing error which will be a multinomial probability
        age_ndx = this_census.age_ndx_[agent_ndx];
        if (apply_ageing_error) {
          LOG_FINEST() << "Agent age = " << age_ndx << " length_ndx = " << length_ndx << " prob correct id = " << mis_matrix[age_ndx][age_ndx];
          float temp_prob = 0.0;
          for (unsigned mis_ndx = 0; mis_ndx < mis_matrix[age_ndx].size(); ++mis_ndx) {
            temp_prob += mis_matrix[age_ndx][mis_ndx];
            if (rng.chance() <= temp_prob) {
              age_ndx = mis_ndx;
              break;
            }
          }
        }
        age_length_key_[age_ndx][length_ndx]++;

        ++total_agents_in_ALK;
        ++sample_attempt;
        this_strata_age_freq[age_ndx]++;
        sampled_numbers_of_lf[length_ndx]++;
        ++age_samples_taken;
      }

      // Systematically iterate through all the cells and agents to fill our number of otoliths
      // Exact same algorithm as above without rng() calls
      if (use_systematic_to_finish_sub_selection) {
        LOG_FINE() << "could not allocate otoliths using probabilisitic methods so using systematic methods";
        // This is a nested loop because we have cells then agents within cells that we are going to loop over
        for (unsigned cell_ndx = 0; cell_ndx < census_stratum_ndx.size(); ++cell_ndx) {
          census_ndx = census_stratum_ndx[cell_ndx];
          processes::census_data& this_census = fishery_year_census[census_ndx];
          // no agents to sample from this cell try again
          if (this_census.age_ndx_.size() <= 0) {
            LOG_FINEST() << "No agents in this cell so try again";
            continue;
          }
          //Agent ndx
          for (unsigned i = 0; i < this_census.age_ndx_.size(); ++i) {
            if (age_samples_taken >= samples_to_take) {
              LOG_FINE() << "we have our quota exit";
              break;
            }
            // We are sampling without replacement so check we haven't sampled this agent.
            for (unsigned check_iter = 0; check_iter < census_sampled.size(); ++check_iter) {
              if ((agents_sampled[check_iter] == i) & (census_sampled[check_iter] == cell_ndx))
                LOG_FINEST() << "we have sampled this agent already, try again";
              continue;
            }
            length_ndx = this_census.length_ndx_[i];

            // apply allocation method
            if (allocation_type_ != AllocationType::kRandom) {
               if (sampled_numbers_of_lf[length_ndx] >= expected_numbers_of_lf[length_ndx]) {
                 // We have selected the neccassary number of agents to age in this length bin try another fish
                 // TODO this gets called alot, can we make this more effecient...
                 LOG_FINEST() << " We have selected the neccassary number of agents to age in this length bin try another fish";
                 continue;
               }
            }

            age_ndx = this_census.age_ndx_[i];
            if (apply_ageing_error) {
              LOG_FINEST() << "Agent age = " << age_ndx << " length_ndx = " << length_ndx << " prob correct id = " << mis_matrix[age_ndx][age_ndx];
              float temp_prob = 0.0;
              for (unsigned mis_ndx = 0; mis_ndx < mis_matrix[age_ndx].size(); ++mis_ndx) {
                temp_prob += mis_matrix[age_ndx][mis_ndx];
                if (rng.chance() <= temp_prob) {
                  age_ndx = mis_ndx;
                  break;
                }
              }
            }
            age_length_key_[age_ndx][length_ndx]++;

            ++total_agents_in_ALK;
            ++sample_attempt;
            this_strata_age_freq[age_ndx]++;
            sampled_numbers_of_lf[length_ndx]++;
            ++age_samples_taken;
          }
        }
      }

      LOG_FINE() << "total number in ALK frequency " << total_agents_in_ALK;
      age_length_key_by_year_stratum_[years_[year_ndx]][cells_[stratum_ndx]] = age_length_key_;
      lf_by_year_stratum_[years_[year_ndx]][cells_[stratum_ndx]] = stratum_length_frequency;
      // Convert ALK to proportions and apply LF
      for (unsigned j = 0; j < model_->length_bin_mid_points().size(); ++j) {
        float length_sum = 0;
        for (unsigned i = 0; i < model_->age_spread(); ++i) {
          length_sum += age_length_key_[i][j];
        }
        LOG_FINE() << "for length bin " << j << " length sum = " << length_sum;
        if (length_sum >= 0) {
          for (unsigned i = 0; i < model_->age_spread(); ++i) {
            if (!utils::doublecompare::IsZero(length_sum)) { // check for divide by zero situation
              age_length_key_[i][j] /= length_sum;
            } else {
              age_length_key_[i][j] = 0.0;
            }
          }
        }
      }


      LOG_FINE() << "about to calculate a frequency via age length key";
      stratum_age_frequency_[cells_[stratum_ndx]].resize(model_->age_spread(),0.0);
      stratum_age_frequency_[cells_[stratum_ndx]].clear();
      for (unsigned age_bin_ndx = 0; age_bin_ndx < model_->age_spread(); ++age_bin_ndx) {
        //LOG_FINE() << "numbers at age before = " << stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx];
        stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx] = 0.0;
        for (unsigned length_bin_ndx = 0; length_bin_ndx < stratum_length_frequency.size(); ++length_bin_ndx) {
          stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx] += age_length_key_[age_bin_ndx][length_bin_ndx] * stratum_length_frequency[length_bin_ndx];
          //LOG_FINE() << "length bin = " << length_bin_ndx << " ALK = " << age_length_key_[age_bin_ndx][length_bin_ndx] << " total length = " << stratum_length_frequency[length_bin_ndx];
        }
        LOG_FINE() << "numbers at age = " << age_bin_ndx + model_->min_age() << " = " << stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx];
        SaveComparison(age_bin_ndx + model_->min_age(), 0, cells_[stratum_ndx], stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx], 0.0, (float)samples_to_take, years_[year_ndx]);
      }

    } // Stratum loop
  } // year loop
  for (auto& iter : comparisons_) {
    for (auto& second_iter : iter.second) {  // cell
      float total = 0.0;
      LOG_FINE() << "cells = " << iter.second.size() << " second_iter.second " << second_iter.second.size();
      for (auto& comparison : second_iter.second)
        total += comparison.expected_;
      LOG_FINE() << "total = " << total;
      for (auto& comparison : second_iter.second)
        comparison.expected_ /= total;
      // No simulation in this, simulated = expected

      for (auto& comparison : second_iter.second)
        comparison.simulated_ = comparison.expected_;
    }
  }

} // DoExecute

} /* namespace observations */
} /* namespace niwa */

