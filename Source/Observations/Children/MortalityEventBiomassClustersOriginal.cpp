/**
 * @file MortalityEventBiomassClustersOriginal.cpp
 * @author  C.Marsh github.com/Craig44
 * @date 4/11/2018
 * @section LICENSE
 *
 *
 */

// headers
#include "MortalityEventBiomassClustersOriginal.h"


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
MortalityEventBiomassClustersOriginal::MortalityEventBiomassClustersOriginal(Model* model) : Observation(model) {
  cluster_sample_table_ = new parameters::Table(PARAM_CLUSTERS_SAMPLED);

  parameters_.BindTable(PARAM_CLUSTERS_SAMPLED, cluster_sample_table_, "Number of clusters samples, by year (row) and stratum (col)", "", false);

  parameters_.Bind<unsigned>(PARAM_YEARS, &years_, "The years of the observed values", "");
  parameters_.Bind<string>(PARAM_AGEING_ERROR, &ageing_error_label_, "Label of ageing error to use", "", PARAM_NONE);
  parameters_.Bind<string>(PARAM_PROCESS_LABEL, &process_label_, "Label of of removal process", "", "");
  parameters_.Bind<string>(PARAM_FISHERY_LABEL, &fishery_label_, "Label of of removal process", "");

  // Cluster Inputs
  parameters_.Bind<float>(PARAM_AVERAGE_CLUSTER_WEIGHT, &average_cluster_weight_, "Mean size in weight of the cluster size could be tow or trip intepretation", "");
  parameters_.Bind<float>(PARAM_CLUSTER_CV, &cluster_cv_, "CV for randomly selecting clusters", "");
  //parameters_.Bind<string>(PARAM_CLUSTER_DISTRIBUTION, &cluster_distribution_, "The distribution for generating random cluster sizes", "", PARAM_NORMAL)->set_allowed_values({PARAM_NORMAL, PARAM_LOGNORMAL});
  parameters_.Bind<string>(PARAM_CLUSTER_ATTRIBUTE, &cluster_attribute_, "What attribute do you want to link clusters by, either age or length", "", PARAM_AGE)->set_allowed_values({PARAM_AGE, PARAM_LENGTH});
  parameters_.Bind<float>(PARAM_CLUSTER_CORRELATION_LAMBDA, &cluster_lambda_, "The probability of being associated to a cluster based on distrance from attribute", "")->set_range(0.0, 1.0, true, true);
  parameters_.Bind<unsigned>(PARAM_AGE_SAMPLES_PER_CLUSTER, &age_samples_per_clusters_, "Number of age samples available to be aged per cluster", "");
  parameters_.Bind<unsigned>(PARAM_LENGTH_SAMPLES_PER_CLUSTER, &length_samples_per_clusters_, "Number of age samples available to be aged per cluster", "");
  parameters_.Bind<float>(PARAM_MINIMUM_CLUSTER_WEIGHT_TO_SAMPLE, &minimum_cluster_weight_to_sample_, "The minimum weight (tonnes) threshold to consider sampling, should be well in the distribution of cluster sizes", "");
  parameters_.Bind<string>(PARAM_FINAL_AGE_PROTOCOL, &ageing_protocol_, "What method do you want to use to calculate final age composition", "", PARAM_AGE_LENGTH_KEY)->set_allowed_values({PARAM_DIRECT_AGEING, PARAM_AGE_LENGTH_KEY});

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
MortalityEventBiomassClustersOriginal::~MortalityEventBiomassClustersOriginal() {
  delete cluster_sample_table_;
}
/**
 *
 */
void MortalityEventBiomassClustersOriginal::DoValidate() {
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
  if (cluster_attribute_ == PARAM_AGE) {
    age_based_clusters_ = true;
  } else {
    age_based_clusters_ = false;
  }


}

/**
 *
 */
void MortalityEventBiomassClustersOriginal::DoBuild() {
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

  // Validate stratum cluster table
  vector<vector<string>>& cluster_data = cluster_sample_table_->data();
  if ((cluster_data.size() - 1) != years_.size()) {
    LOG_ERROR_P(PARAM_CLUSTERS_SAMPLED) << " has " << (cluster_data.size() - 1) << " rows defined, but we expected " << years_.size()
        << " to match the number of years provided";
  }
  vector<string>  stratum_order_from_cluster_table = cluster_data[0];

  if (stratum_order_from_cluster_table[0] != PARAM_YEAR)
    LOG_FATAL_P(PARAM_CLUSTERS_SAMPLED) << "Expected the first column to have the following header '" << PARAM_YEAR << "'";

  for (unsigned col_ndx = 1; col_ndx < stratum_order_from_cluster_table.size(); ++col_ndx) {
    if (find(cells_.begin(),cells_.end(),stratum_order_from_cluster_table[col_ndx]) == cells_.end())
      LOG_FATAL_P(PARAM_CLUSTERS_SAMPLED) << "Could not find the stratum '" << stratum_order_from_cluster_table[col_ndx] << "' (colum header '" << col_ndx + 1 << "') in the parameter " << PARAM_STRATUMS_TO_INCLUDE << " can you please check that the column headers are consistent with this parameter, chairs";
  }

  LOG_FINE() << "rows in cluster table = " << cluster_data.size();
  for (unsigned row_counter = 1; row_counter < cluster_data.size(); ++row_counter) {
    if (cluster_data[row_counter].size() != (cells_.size() + 1)) {
      LOG_FATAL_P(PARAM_CLUSTERS_SAMPLED) << " has " << cluster_data[row_counter].size() << " values defined, but we expected " << cells_.size() + 1
          << " to match a number for each stratum";
    }
    unsigned year = 0;
    if (!utilities::To<unsigned>(cluster_data[row_counter][0], year))
      LOG_ERROR_P(PARAM_CLUSTERS_SAMPLED) << " value " << cluster_data[row_counter][0] << " could not be converted in to an unsigned integer. It should be the year for this line";
    if (std::find(years_.begin(), years_.end(), year) == years_.end())
      LOG_ERROR_P(PARAM_CLUSTERS_SAMPLED) << " value " << year << " is not a valid year for this observation";

    LOG_FINE() << "Year = " << year << " in table = " << cluster_data[row_counter][0];
    for (unsigned i = 1; i < cluster_data[row_counter].size(); ++i) {
      unsigned value = 0;

      if (!utilities::To<unsigned>(cluster_data[row_counter][i], value))
        LOG_FATAL_P(PARAM_CLUSTERS_SAMPLED) << " value (" << cluster_data[row_counter][i] << " at row = " << row_counter << " and column " << i << " could not be converted to an integer";
      if (value < 0) {
        LOG_ERROR_P(PARAM_CLUSTERS_SAMPLED) << "at row = " << row_counter << " and column " << i << " the value given = " << value << " this needs to be an integer greater than 0.";
      }
      LOG_FINE() << "stratum = " << stratum_order_from_cluster_table[i] << " samples = " << value;

      cluster_by_year_and_stratum_[year][stratum_order_from_cluster_table[i]] = value;
    }
  }

  if (ageing_allocation_ == PARAM_EQUAL)
    allocation_type_ = AllocationType::kEqual;
  else if (ageing_allocation_ == PARAM_PROPORTIONAL)
    allocation_type_ = AllocationType::kProportional;

  cluster_length_freq_.resize(model_->number_of_length_bins(), 0);
  stratum_lf_.resize(model_->number_of_length_bins(), 0);
  stratum_af_.resize(model_->age_spread(), 0);
  expected_aged_length_freq_.resize(model_->number_of_length_bins(), 0);
  sampled_aged_length_freq_.resize(model_->number_of_length_bins(), 0);
  agent_ndx_for_length_subsample_within_cluster_.resize(length_samples_per_clusters_, 0);
  agent_ndx_for_age_subsample_within_cluster_.resize(age_samples_per_clusters_, 0);

  age_length_key_.resize(model_->age_spread());
  for (unsigned i = 0; i < model_->age_spread(); ++i)
    age_length_key_[i].resize(model_->number_of_length_bins(),0.0);

  cluster_census_.resize(years_.size());
  cluster_length_samples_.resize(years_.size());
  cluster_weight_.resize(years_.size());
  cluster_mean_.resize(years_.size());
  cluster_age_samples_.resize(years_.size());
  for(unsigned i = 0; i < years_.size(); ++i) {
    cluster_census_[i].resize(cells_.size());
    cluster_length_samples_[i].resize(cells_.size());
    cluster_age_samples_[i].resize(cells_.size());
    cluster_weight_[i].resize(cells_.size());
    cluster_mean_[i].resize(cells_.size());
    for(unsigned j = 0; j < cells_.size(); ++j) {
      cluster_census_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]]);
      cluster_length_samples_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]]);
      cluster_age_samples_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]]);
      cluster_weight_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]],0.0);
      cluster_mean_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]],0.0);
      for(unsigned k = 0; k < cluster_by_year_and_stratum_[years_[i]][cells_[j]]; ++k) {
        cluster_length_samples_[i][j][k].resize(length_samples_per_clusters_,0);
        cluster_age_samples_[i][j][k].resize(age_samples_per_clusters_,0);
      }
    }
  }

  model_length_mid_points_ = model_->length_bin_mid_points();
}

/**
 *
 */
void MortalityEventBiomassClustersOriginal::PreExecute() {

}

/**
 *
 */
void MortalityEventBiomassClustersOriginal::Execute() {

}
/**
 *  Reset dynamic containers, in between simulations
 */
void MortalityEventBiomassClustersOriginal::ResetPreSimulation() {
  for(unsigned i = 0; i < years_.size(); ++i) {
    for(unsigned j = 0; j < cells_.size(); ++j) {
      fill(cluster_weight_[i][j].begin(), cluster_weight_[i][j].end(),0.0);
      fill(cluster_mean_[i][j].begin(), cluster_mean_[i][j].end(),0.0);
      for(unsigned k = 0; k < cluster_by_year_and_stratum_[years_[i]][cells_[j]]; ++k) {
        cluster_census_[i][j][k].clear();
        fill(cluster_length_samples_[i][j][k].begin(),cluster_length_samples_[i][j][k].end(), 0);
        fill(cluster_age_samples_[i][j][k].begin(),cluster_age_samples_[i][j][k].end(), 0);
      }
    }
  }
  ClearComparison(); // Clear comparisons

}

/**
 *
 */
void MortalityEventBiomassClustersOriginal::Simulate() {
  LOG_MEDIUM() << "Simulating data for observation = " << label_;
  if (model_->run_mode() != (RunMode::Type)(RunMode::kMSE))
    ResetPreSimulation();
  LOG_MEDIUM() << "size clusters " << comparisons_.size();
  utilities::RandomNumberGenerator& rng = utilities::RandomNumberGenerator::Instance();
  vector<vector<processes::census_data>> fishery_age_data = mortality_process_->get_fishery_census_data(fishery_label_);

  if (fishery_age_data.size() == 0) {
    LOG_CODE_ERROR() << "could not return information for fishery " << fishery_label_ << " this error should be dealt with earlier in the code";
  }

  //vector<processes::composition_data>& length_frequency = mortality_process_->get_removals_by_length();
  LOG_FINE() << "length of census data (years * cells) = " << fishery_age_data.size();

  vector<unsigned> census_stratum_ndx;

  bool apply_ageing_error = true;
  if (!ageing_error_)
    apply_ageing_error = false;

  vector<vector<float>> mis_matrix;
  if (apply_ageing_error)
     mis_matrix = ageing_error_->mis_matrix();


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
    for (unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      LOG_FINE() << "About to sort our info for stratum " << cells_[stratum_ndx];
      vector<float> stratum_length_frequency(model_->length_bin_mid_points().size(),0.0);
      unsigned clusters_to_sampled = cluster_by_year_and_stratum_[years_[year_ndx]][cells_[stratum_ndx]];
      agent_ndx_cluster_.resize(clusters_to_sampled);
      census_ndx_cluster_.resize(clusters_to_sampled);
      for(unsigned i = 0; i < clusters_to_sampled; ++i) {
        agent_ndx_cluster_[i].clear();
        census_ndx_cluster_[i].clear();
      }
      LOG_FINE() << "clusters to sample " << clusters_to_sampled << " - " << census_ndx_cluster_.size();
      census_stratum_ndx.clear();
      unsigned total_agents_in_ALK = 0;
      // Reset Stratum Objects
      fill(stratum_lf_.begin(),stratum_lf_.end(), 0);
      for (unsigned i = 0; i < model_->age_spread(); ++i)
        fill(age_length_key_[i].begin(),age_length_key_[i].end(), 0);

      stratum_biomass_[cells_[stratum_ndx]] = 0.0;
      fill(stratum_af_.begin(),stratum_af_.end(), 0);
      fill(stratum_lf_.begin(),stratum_lf_.end(), 0);

      // -- Find which census objects relate to this year and stratum
      //    save that information to do a look up later.
      // -- if more than one cell in stratum find biomass weights by sex
      unsigned census_ndx = 0; // links back to the census
      LOG_FINE() << "number of cells in this year and fishery = " << fishery_year_census.size();
      vector<vector<unsigned>> agents_ndx_measured_for_length(fishery_year_census.size());
      float total_stratum_biomass = 0.0;
      vector<float> biomass_by_cell;
      for (processes::census_data& census : fishery_year_census) {
        // Find census elements that are in this stratum
        if ((find(stratum_rows_[cells_[stratum_ndx]].begin(),stratum_rows_[cells_[stratum_ndx]].end(), census.row_) != stratum_rows_[cells_[stratum_ndx]].end()) && (find(stratum_cols_[cells_[stratum_ndx]].begin(),stratum_cols_[cells_[stratum_ndx]].end(), census.col_) != stratum_cols_[cells_[stratum_ndx]].end())) {
          if (census.age_ndx_.size() > 0) {
            //LOG_FINE() << "found a census that year and cell work, agents in it = " << census.age_ndx_.size() << " proportion to take = " << clusters_to_sampled;
            census_stratum_ndx.push_back(census_ndx);
            // Calculate the agents available in the first sampling unit
            LOG_FINE() << "agents in this cell = " << census.age_ndx_.size();
            if (sexed_flag_) {
              if (sex_match_ == 0) {
                total_stratum_biomass += census.biomass_;
                biomass_by_cell.push_back(census.biomass_);
              } else {
                total_stratum_biomass += census.female_biomass_;
                biomass_by_cell.push_back(census.female_biomass_);
              }
            } else {
              total_stratum_biomass += census.biomass_;
              biomass_by_cell.push_back(census.biomass_);
            }
          }
        }
        ++census_ndx;
      }
      LOG_FINE() << "census objects in this stratum = " << census_stratum_ndx.size();

      // If multiple cells in stratum then find out the distribution of sampled clusters by cell
      vector<unsigned> clusters_to_sample_by_cell;
      for (auto & cell_bio : biomass_by_cell) {
        cell_bio /= total_stratum_biomass;
        LOG_FINE() << "prop of clusters to sample from this cell = " << cell_bio;
        clusters_to_sample_by_cell.push_back(cell_bio * clusters_to_sampled);
      }
      for (auto & cluster :clusters_to_sample_by_cell)
        LOG_FINE() << "cluster samples for each cell " << cluster;
      // -------------------
      // Generate clusters
      // For each cell
      // Four versions of the same code, for different distributions (normal and lognormal) and different cluster attributes (age, length)
      // -------------------
      float cluster_size = 0, cluster_original_size = 0;
      unsigned attempt = 0;
      unsigned max_attempts = 1000;
      unsigned agents_available = 1000;
      unsigned agent_ndx = 0;
      float cluster_mean = 0.0;
      unsigned total_cluster_ndx = 0;
      for (unsigned cell_ndx = 0; cell_ndx < census_stratum_ndx.size(); ++cell_ndx) {
        unsigned clusters_to_collate = clusters_to_sample_by_cell[cell_ndx];
        processes::census_data& census = fishery_year_census[census_stratum_ndx[cell_ndx]];
        LOG_FINE() << "Biomass available in this cell = " << census.biomass_;
        agents_available = census.age_ndx_.size();
        unsigned agent_counter = 0;
        for (unsigned cluster_ndx = 0; cluster_ndx < clusters_to_collate; ++cluster_ndx) {
          LOG_FINE() << "cluster ndx = " << total_cluster_ndx;
          max_attempts = agents_available * 20;

          attempt = 0;
          // Find a cluster size that is greater than mimimum weight threshold and take into account
          // that each trial was a missed cluster and so there is a finite number of attempts based on total_stratum_biomass;
          while ((cluster_size < minimum_cluster_weight_to_sample_) | (total_stratum_biomass > 0)) {
            cluster_size = rng.lognormal(average_cluster_weight_, cluster_cv_);
            total_stratum_biomass -= cluster_size;
          }


          // Draw the first value of each cluster
          agent_ndx = agents_available * rng.chance();
          if (age_based_clusters_)
            cluster_mean = census.age_ndx_[agent_ndx];
          else
            cluster_mean = model_length_mid_points_[census.length_ndx_[agent_ndx]];

          cluster_mean_[year_ndx][stratum_ndx][cluster_ndx] = cluster_mean;
          cluster_weight_[year_ndx][stratum_ndx][cluster_ndx] = cluster_size;

          agent_ndx_cluster_[total_cluster_ndx].push_back(agent_ndx);
          census_ndx_cluster_[total_cluster_ndx].push_back(census_stratum_ndx[cell_ndx]);
          LOG_FINE() <<"max_attempts " << max_attempts <<  " cluster mean = " << cluster_mean << " cluster size = " << cluster_size << ", number of agents = " << census.age_ndx_.size();

          cluster_original_size = cluster_size;

          if(census_ndx_cluster_[total_cluster_ndx].size() > 1) {
            LOG_CODE_ERROR() << "'census_ndx_cluster_[total_cluster_ndx].size() > 1', this should be 1 something is going wrong it is = " << census_ndx_cluster_[total_cluster_ndx].size() << " it means this observation has been previously run";
          }
          while (cluster_size > 0) {
            ++attempt;
            if (attempt > max_attempts) {
              LOG_WARNING() << "Observation " << label_ << " Not enough agents to build cluster, tried to 5 x the number of agents available, either not enough agents or the cluster requirements to computationslly difficult  e.g. too high lambda. Weight (tonnes) left to add to this cluster = " << cluster_size << " we wanted " << cluster_original_size;
              break;
            }

            agent_ndx = agents_available * rng.chance();
            // check we haven't sampled this agent or should we let it fly, because they represent multiple individuals?
            /*
            if (find(agent_ndx_cluster_[total_cluster_ndx].begin(), agent_ndx_cluster_[total_cluster_ndx].end(), agent_ndx) != agent_ndx_cluster_[total_cluster_ndx].end())
              continue;
            */
            // check we haven't sampled this agent or should we let it fly, because they represent multiple individuals?
            if (age_based_clusters_) {
              if (rng.chance() < exp(-cluster_lambda_ * fabs(census.age_ndx_[agent_ndx] - cluster_mean))) {
                agent_ndx_cluster_[total_cluster_ndx].push_back(agent_ndx);
                census_ndx_cluster_[total_cluster_ndx].push_back(census_stratum_ndx[cell_ndx]);
                cluster_census_[year_ndx][stratum_ndx][cluster_ndx].push_back(census.age_ndx_[agent_ndx]);
                cluster_size -= census.weight_[agent_ndx];
                ++agent_counter;
              }
            } else {
              if (rng.chance() < exp(-cluster_lambda_ * fabs(model_length_mid_points_[census.length_ndx_[agent_ndx]] - cluster_mean))) {
                agent_ndx_cluster_[total_cluster_ndx].push_back(agent_ndx);
                census_ndx_cluster_[total_cluster_ndx].push_back(census_stratum_ndx[cell_ndx]);
                cluster_size -= census.weight_[agent_ndx];
                cluster_census_[year_ndx][stratum_ndx][cluster_ndx].push_back(census.age_ndx_[agent_ndx]);
                ++agent_counter;
              }
            }
          }
          LOG_FINE() << "attempts to calculate cluster = " << attempt;

          if (agent_counter < length_samples_per_clusters_) {
            LOG_WARNING() << "for observation " << label_ << ", the number of agents in cluster '" << total_cluster_ndx << "' = " << agent_counter << " you want " << length_samples_per_clusters_ << " lengths sampled, this ain't going to work. Maybe you need to add more agents into the system";
          }

          //-----------------------------------------
          // Now lets sub-sample lengths from cluster
          //-----------------------------------------

          fill(cluster_length_freq_.begin(),cluster_length_freq_.end(), 0);
          fill(expected_aged_length_freq_.begin(),expected_aged_length_freq_.end(), 0);
          fill(sampled_aged_length_freq_.begin(),sampled_aged_length_freq_.end(), 0);
          // Perhaps not appropriate to fill with 0's this means the first agent will never get picked... probably worth the memory allocation
          // time saving.
          fill(agent_ndx_for_length_subsample_within_cluster_.begin(), agent_ndx_for_length_subsample_within_cluster_.end(), 0);
          fill(agent_ndx_for_age_subsample_within_cluster_.begin(), agent_ndx_for_age_subsample_within_cluster_.end(), 0);

          unsigned lengths_to_sample = length_samples_per_clusters_;
          unsigned slot = 0;
          unsigned size_int_for_random_sampling = agent_ndx_cluster_[total_cluster_ndx].size();
          attempt = 0;
          bool skip_iter = false;
          max_attempts = size_int_for_random_sampling * 5;
          LOG_FINE() << "number available for sampling = " << size_int_for_random_sampling << " lengths allowed = " << cluster_length_freq_.size();
          LOG_FINE() << "number of agents in census = " << census.length_ndx_.size();
          LOG_FINE() << "cluster ndx = " << total_cluster_ndx << " - " << cluster_ndx;
          unsigned sample_ndx = 0;

          while (lengths_to_sample > 0) {
            ++attempt;
            //skip_iter = false;
            if (attempt > max_attempts) {
              LOG_WARNING() << "could not sample all lengths in observation " << label_ << " for cluster = " << total_cluster_ndx;
              break;
            }
            sample_ndx = size_int_for_random_sampling * rng.chance();

            agent_ndx = agent_ndx_cluster_[total_cluster_ndx][sample_ndx];
            /*
            // Check we haven't already selected this agent for lengths
            for(unsigned i = 0; i < slot; ++i) {
              if (agent_ndx == agent_ndx_for_length_subsample_within_cluster_[i]) {
                skip_iter = true;
                break;
              }
            }
            if (skip_iter)
              continue;
            */
            agent_ndx_for_length_subsample_within_cluster_[slot] = agent_ndx;

            cluster_length_freq_[census.length_ndx_[agent_ndx]]++;

            stratum_lf_[census.length_ndx_[agent_ndx]]++;

            cluster_length_samples_[year_ndx][stratum_ndx][cluster_ndx][slot] = census.length_ndx_[agent_ndx];

            --lengths_to_sample;
            ++slot;
          }
          LOG_FINE() << "attempts = " << attempt;
          // Now lets sub-sample ages from length samples
          float non_zero_length_bins = 0;
          // number of length bins with non-zero entry
          float tot = 0;
          for (auto& len : cluster_length_freq_) {
            if (len > 0)
              ++non_zero_length_bins;
            tot += len;
          }
          unsigned total_expect = 0;
          LOG_FINE() << "number in individuals in length distribution = " << tot;
          if (allocation_type_ == AllocationType::kEqual) {
            for (unsigned len_bin = 0; len_bin < cluster_length_freq_.size(); ++len_bin) {
              if (cluster_length_freq_[len_bin] > 0) {
                expected_aged_length_freq_[len_bin] = (unsigned) (round((float)age_samples_per_clusters_ * (1/non_zero_length_bins)));
                total_expect += expected_aged_length_freq_[len_bin];
                if (total_expect > age_samples_per_clusters_) {
                  expected_aged_length_freq_[len_bin] -= (total_expect - age_samples_per_clusters_);
                  total_expect  -=  (total_expect - age_samples_per_clusters_);
                }
              }
            }
            if(total_expect != age_samples_per_clusters_)
              LOG_WARNING() << "suppose to tage " << age_samples_per_clusters_ << " age samples but code wants to take " << total_expect << " error in the code";

          } else if (allocation_type_ == AllocationType::kProportional) {
            for (unsigned len_bin = 0; len_bin < cluster_length_freq_.size(); ++len_bin) {
              if (cluster_length_freq_[len_bin] > 0) {
                expected_aged_length_freq_[len_bin] = (unsigned) (round((float)age_samples_per_clusters_ * (cluster_length_freq_[len_bin]/tot)));
                total_expect += expected_aged_length_freq_[len_bin];
                LOG_FINE() << "total = " << total_expect << " number = " << expected_aged_length_freq_[len_bin] << " proportion = " << (cluster_length_freq_[len_bin]/tot);
                if (total_expect > age_samples_per_clusters_) {
                  expected_aged_length_freq_[len_bin] -= (total_expect - age_samples_per_clusters_);
                  total_expect  -=  (total_expect - age_samples_per_clusters_);
                }
              }
            }
            if(total_expect != age_samples_per_clusters_)
              LOG_WARNING() << "suppose to tage " << age_samples_per_clusters_ << " age samples but code wants to take " << total_expect << " error in the code";

          }


          unsigned age_samples_taken = 0;
          unsigned age_ndx = 0;
          unsigned length_ndx = 0;
          attempt = 0;
          max_attempts = length_samples_per_clusters_ * 5;
          bool use_systematic_to_finish_sub_selection = false;
          // First try and get all otoliths based on allocation method,
          // if two difficult (constituted by 5 x number length samples) just randomly get the rest.
          LOG_FINE() << "max attempts = " << max_attempts << " samples to take = " << age_samples_per_clusters_ << " attempt = " << attempt;
          LOG_FINE() << "number of agents sampled for lengths = " << agent_ndx_for_length_subsample_within_cluster_.size();
          unsigned check_len_ndx = 0;
          size_int_for_random_sampling = agent_ndx_for_length_subsample_within_cluster_.size();
          while (age_samples_taken < age_samples_per_clusters_) {
            ++attempt;
            skip_iter = false;

            if (attempt > max_attempts) {
              use_systematic_to_finish_sub_selection = true;
              break;
            }
            check_len_ndx =  size_int_for_random_sampling * rng.chance();
            agent_ndx = agent_ndx_for_length_subsample_within_cluster_[check_len_ndx];

            // Check we haven't already aged this agent from the length sample
            for (unsigned check_iter = 0; check_iter < age_samples_taken; ++check_iter) {
              if ((agent_ndx_for_age_subsample_within_cluster_[check_iter] == agent_ndx)) {
                LOG_FINE() << "we have sampled this agent already, try again " << agent_ndx_for_age_subsample_within_cluster_[check_iter] << " actual index = " << agent_ndx << " attempt = " << attempt;
                skip_iter = true;
                break;
              }
            }
            if(skip_iter)
              continue;

            length_ndx = census.length_ndx_[agent_ndx];
            // apply allocation method
            if (allocation_type_ != AllocationType::kRandom) {
               if (sampled_aged_length_freq_[length_ndx] >= expected_aged_length_freq_[length_ndx]) {
                 // We have selected the neccassary number of agents to age in this length bin try another fish
                 skip_iter = true;
                 continue;
               }
            }
            if(skip_iter)
              continue;
            //------------------------------
            // We are going to age this agent
            //------------------------------
            agent_ndx_for_age_subsample_within_cluster_[age_samples_taken] = agent_ndx;
            sampled_aged_length_freq_[length_ndx]++;

            // Are we applying ageing error which will be a multinomial probability
            age_ndx = census.age_ndx_[agent_ndx];

            // Note this is pre age misspecification
            cluster_age_samples_[year_ndx][stratum_ndx][cluster_ndx][age_samples_taken] = age_ndx;

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
            stratum_af_[age_ndx]++;
            ++total_agents_in_ALK;
            ++age_samples_taken;
            LOG_FINE() << age_samples_taken;

          }

          // Only enter this if we couldn't (within reasonable time) find the right age samples to match length distributions
          if (use_systematic_to_finish_sub_selection) {
            LOG_FINE() << "need to systematically select fish so that we get our desired number";
            for (unsigned i = 0; i < agent_ndx_for_length_subsample_within_cluster_.size(); ++i) {
              if (age_samples_taken >= age_samples_per_clusters_) {
                LOG_FINE() << "we have our quota exit";
                break;
              }

              agent_ndx = agent_ndx_for_length_subsample_within_cluster_[i];
              /*
              // Check we haven't already sampled this agent
              for (unsigned check_iter = 0; check_iter <= age_samples_taken; ++check_iter) {
                if ((agent_ndx_for_age_subsample_within_cluster_[check_iter] == agent_ndx))
                  LOG_FINE() << "we have sampled this agent already, try again";
                continue;
              }
              */
              length_ndx = census.length_ndx_[agent_ndx];
              // apply allocation method
              if (allocation_type_ != AllocationType::kRandom) {
                 if (sampled_aged_length_freq_[length_ndx] >= expected_aged_length_freq_[length_ndx]) {
                   // We have selected the neccassary number of agents to age in this length bin try another fish
                   continue;
                 }
              }

              //------------------------------
              // We are going to age this agent
              //------------------------------
              agent_ndx_for_age_subsample_within_cluster_[age_samples_taken] = agent_ndx;
              sampled_aged_length_freq_[length_ndx]++;
              age_ndx = census.age_ndx_[agent_ndx];

              cluster_age_samples_[year_ndx][stratum_ndx][cluster_ndx][age_samples_taken] = age_ndx;


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
              stratum_af_[age_ndx]++;
              ++age_samples_taken;
              ++total_agents_in_ALK;
              LOG_FINE() << age_samples_taken;

            }
          }
/*          LOG_MEDIUM() << "expected LF for age samples = \n";
          for(auto & ecpect_val : expected_aged_length_freq_)
            std::cerr << ecpect_val << " ";
          LOG_MEDIUM() << "\n sampled LF for age samples = ";
          for(auto & sampled_val : sampled_aged_length_freq_)
            std::cerr << sampled_val << " ";
          std::cerr << "\n";
*/


          total_cluster_ndx++;
        } // clusters within each cell
      } // cells
      LOG_FINE() << "total number in Age-length Key " << total_agents_in_ALK;
      age_length_key_by_year_stratum_[years_[year_ndx]][cells_[stratum_ndx]] = age_length_key_;
      lf_by_year_stratum_[years_[year_ndx]][cells_[stratum_ndx]] = stratum_lf_;
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
        LOG_FINE() << "numbers at age before = " << stratum_af_[age_bin_ndx];
        stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx] = 0.0;
        if (ageing_protocol_ == PARAM_DIRECT_AGEING) {
          SaveComparison(age_bin_ndx + model_->min_age(), 0, cells_[stratum_ndx], stratum_af_[age_bin_ndx], 0.0, 0, years_[year_ndx]);

        } else if (ageing_protocol_ == PARAM_AGE_LENGTH_KEY) {
          for (unsigned length_bin_ndx = 0; length_bin_ndx < stratum_length_frequency.size(); ++length_bin_ndx) {
            stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx] += age_length_key_[age_bin_ndx][length_bin_ndx] * stratum_lf_[length_bin_ndx];
            LOG_FINE() << "length bin = " << length_bin_ndx << " ALK = " << age_length_key_[age_bin_ndx][length_bin_ndx] << " total length = " << stratum_lf_[length_bin_ndx];
          }
          LOG_FINE() << "numbers at age = " << age_bin_ndx + model_->min_age() << " = " << stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx];
          SaveComparison(age_bin_ndx + model_->min_age(), 0, cells_[stratum_ndx], stratum_age_frequency_[cells_[stratum_ndx]][age_bin_ndx], 0.0, (float)total_agents_in_ALK, years_[year_ndx]);
        }
      }
    } // Stratum loop
  } // year loop
  for (auto& iter : comparisons_) {
    for (auto& second_iter : iter.second) {  // cell
      float total = 0.0;
      for (auto& comparison : second_iter.second)
        total += comparison.expected_;
      for (auto& comparison : second_iter.second)
        comparison.expected_ /= total;
      // No simulation in this, simulated = expected
      for (auto& comparison : second_iter.second)
        comparison.simulated_ = comparison.expected_;
    }
  }
} // Simulate



void MortalityEventBiomassClustersOriginal::FillReportCache(ostringstream& cache) {
  // Print the age length key for curiosity
  cache << "length_freq_by_year_stratum "  <<REPORT_R_DATAFRAME <<"\n";
  cache << "year_stratum ";
  for (auto len : model_->length_bin_mid_points())
    cache << len << " ";
  cache << "\n";

  for(auto& len_freq_by_year : lf_by_year_stratum_) {
    for(auto len_freq_by_year_strata : len_freq_by_year.second) {
      cache << len_freq_by_year.first << "_" << len_freq_by_year_strata.first << " ";
      for (unsigned i = 0; i < len_freq_by_year_strata.second.size(); ++i) {
        cache << len_freq_by_year_strata.second[i] << " ";
      }
      cache << "\n";
    }
  }

  for(auto& age_length_key_by_year_stratum__by_year : age_length_key_by_year_stratum_) {
    for(auto age_length_key_by_year_stratum__by_strata : age_length_key_by_year_stratum__by_year.second) {
      cache << "age_length_key_by_year_stratum__" << age_length_key_by_year_stratum__by_year.first << "_" << age_length_key_by_year_stratum__by_strata.first << " " <<REPORT_R_MATRIX <<"\n";
      for (unsigned i = 0; i < age_length_key_by_year_stratum__by_strata.second.size(); ++i) {
        for (unsigned j = 0; j < age_length_key_by_year_stratum__by_strata.second[i].size(); ++j) {
          cache << age_length_key_by_year_stratum__by_strata.second[i][j] << " ";
        }
        cache << "\n";
      }
    }
  }


  // Cluster weight
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    cache << "cluster_biomass-" << years_[year_ndx] << " "  << REPORT_R_MATRIX <<"\n";
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_weight_[year_ndx][stratum_ndx].size(); ++cluster_ndx)
        cache << cluster_weight_[year_ndx][stratum_ndx][cluster_ndx] << " ";
      cache << "\n";
    }
  }
  // Cluster mean
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    cache << "cluster_mean-" << years_[year_ndx] << " " << REPORT_R_MATRIX <<"\n";
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_mean_[year_ndx][stratum_ndx].size(); ++cluster_ndx)
        cache << cluster_mean_[year_ndx][stratum_ndx][cluster_ndx] << " ";
      cache << "\n";
    }
  }

  // Cluster age sample
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      cache << "cluster_age_sample-" << years_[year_ndx] << "-" << cells_[stratum_ndx] << " "  << REPORT_R_MATRIX <<"\n";
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_age_samples_[year_ndx][stratum_ndx].size(); ++cluster_ndx) {
        //cache << cluster_ndx + 1 << " ";
        for (auto& cluster_val : cluster_age_samples_[year_ndx][stratum_ndx][cluster_ndx])
          cache << cluster_val << " ";
        cache << "\n";
      }
    }
  }

  // Cluster length sample
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      cache << "cluster_length_sample-" << years_[year_ndx] << "-" << cells_[stratum_ndx] << " "  << REPORT_R_MATRIX <<"\n";
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_length_samples_[year_ndx][stratum_ndx].size(); ++cluster_ndx) {
        //cache << cluster_ndx + 1 << " ";
        for (auto& cluster_val : cluster_length_samples_[year_ndx][stratum_ndx][cluster_ndx])
          cache << cluster_val << " ";
        cache << "\n";
      }
    }
  }

  // Cluster Census
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_census_[year_ndx][stratum_ndx].size(); ++cluster_ndx) {
        cache << "cluster_census_age-"<< years_[year_ndx] << "-" << cells_[stratum_ndx] << "-" << cluster_ndx + 1 <<  ": ";
        for (auto& cluster_val : cluster_census_[year_ndx][stratum_ndx][cluster_ndx])
          cache << cluster_val << " ";
        cache << "\n";
      }
    }
  }





}

} /* namespace observations */
} /* namespace niwa */

