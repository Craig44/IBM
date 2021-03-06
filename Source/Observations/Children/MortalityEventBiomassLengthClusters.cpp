/**
 * @file MortalityEventBiomassLengthClusters.cpp
 * @author  C.Marsh github.com/Craig44
 * @date 4/11/2018
 * @section LICENSE
 *
 *
 */

// headers
#include "MortalityEventBiomassLengthClusters.h"


#include "Processes/Manager.h"
#include "Layers/Manager.h"
#include "AgeingErrors/Manager.h"

#include "World/WorldView.h"
#include "World/WorldCell.h"

#include <omp.h>

#include "Utilities/RandomNumberGenerator.h"
#include "Utilities/To.h"
#include "Utilities/Map.h"
#include "Utilities/Math.h"
#include "Utilities/DoubleCompare.h"

// namespaces
namespace niwa {
namespace observations {

namespace utils = niwa::utilities;
namespace math = niwa::utilities::math;


/**
 * Default constructor
 */
MortalityEventBiomassLengthClusters::MortalityEventBiomassLengthClusters(Model* model) : Observation(model) {
  cluster_sample_table_ = new parameters::Table(PARAM_CLUSTERS_SAMPLED);

  parameters_.BindTable(PARAM_CLUSTERS_SAMPLED, cluster_sample_table_, "Number of clusters samples, by year (row) and stratum (col)", "", false);
  parameters_.Bind<unsigned>(PARAM_YEARS, &years_, "The years of the observed values", "");
  parameters_.Bind<string>(PARAM_PROCESS_LABEL, &process_label_, "Label of of removal process", "", "");
  parameters_.Bind<string>(PARAM_FISHERY_LABEL, &fishery_label_, "Label of of removal process", "");
  // Cluster Inputs
  parameters_.Bind<float>(PARAM_AVERAGE_CLUSTER_WEIGHT, &average_cluster_weight_, "Mean size in weight of the cluster size could be tow or trip intepretation", "")->set_lower_bound(1.0, false);
  parameters_.Bind<float>(PARAM_CLUSTER_CV, &cluster_cv_, "CV for randomly selecting clusters", "");
  parameters_.Bind<string>(PARAM_COMPOSITION_TYPE, &obs_type_, "", "Do you want  observation as proportions (normalised to sum to one), or as numbers?", PARAM_PROPORTIONS)->set_allowed_values({PARAM_PROPORTIONS,PARAM_NUMBERS});

  //parameters_.Bind<string>(PARAM_CLUSTER_DISTRIBUTION, &cluster_distribution_, "The distribution for generating random cluster sizes", "", PARAM_NORMAL)->set_allowed_values({PARAM_NORMAL, PARAM_LOGNORMAL});
  parameters_.Bind<float>(PARAM_CLUSTER_SIGMA, &cluster_sigma_, "Standard deviation for the M-H proposal distribution", "")->set_lower_bound(0.0, false);
  parameters_.Bind<unsigned>(PARAM_LENGTH_SAMPLES_PER_CLUSTER, &length_samples_per_clusters_, "Number of age samples available to be aged per cluster", "");
  //parameters_.Bind<float>(PARAM_MINIMUM_CLUSTER_WEIGHT_TO_SAMPLE, &minimum_cluster_weight_to_sample_, "The minimum weight (tonnes) threshold to consider sampling, should be well in the distribution of cluster sizes", "");
  //parameters_.Bind<string>(PARAM_FINAL_AGE_PROTOCOL, &ageing_protocol_, "What method do you want to use to calculate final age composition", "", PARAM_AGE_LENGTH_KEY)->set_allowed_values({PARAM_DIRECT_AGEING, PARAM_AGE_LENGTH_KEY});


  //parameters_.Bind<string>(PARAM_AGEING_ALLOCATION_METHOD, &ageing_allocation_, "The method used to allocate aged individuals across the length distribution", "", PARAM_RANDOM)->set_allowed_values({PARAM_RANDOM,PARAM_EQUAL,PARAM_PROPORTIONAL});
  // TODO add these in at some point ...
  //parameters_.Bind<unsigned>(PARAM_NUMBER_OF_BOOTSTRAPS, &number_of_bootstraps_, "Number of bootstraps to conduct for each stratum to calculate Pooled CV's for each stratum and total age frequency", "", 50);
  parameters_.Bind<string>(PARAM_STRATUM_WEIGHT_METHOD, &stratum_weight_method_, "Method to weight stratum estimates by", "", PARAM_BIOMASS)->set_allowed_values({PARAM_BIOMASS, PARAM_AREA, PARAM_NONE});
  parameters_.Bind<bool>(PARAM_SEXED, &sexed_flag_, "You can ask to 'ignore' sex (only option for unsexed model), or generate composition for a particular sex, either 'male' or 'female", "", false);

  parameters_.Bind<string>(PARAM_LAYER_OF_STRATUM_DEFINITIONS, &layer_label_, "The layer that indicates what the stratum boundaries are.", "");
  parameters_.Bind<string>(PARAM_STRATUMS_TO_INCLUDE, &cells_, "The cells which represent individual stratum to be included in the analysis, default is all cells are used from the layer", "", true);
}
/**
 * Destructor
 */
MortalityEventBiomassLengthClusters::~MortalityEventBiomassLengthClusters() {
  delete cluster_sample_table_;
}
/**
 *
 */
void MortalityEventBiomassLengthClusters::DoValidate() {
  LOG_TRACE();
  for (auto year : years_) {
    LOG_FINE() << "year : " << year;
    if ((year < model_->start_year()) || (year > model_->final_year()))
      LOG_ERROR_P(PARAM_YEARS) << "Years can't be less than start_year (" << model_->start_year() << "), or greater than final_year (" << model_->final_year()
          << "). Please fix this.";
  }

  if(!model_->get_sexed()) {
    if (sexed_flag_) {
      LOG_WARNING() << "you asked for a sexed observation but the model isn't sexed so I am ignoring this and giving you unsexed results.";
      sexed_flag_ = false;
    }
  }

  if (obs_type_ == PARAM_NUMBERS)
    are_obs_props_ = false;
  LOG_MEDIUM() << "comp type = " << obs_type_ << " bool = " << are_obs_props_ << " 0 = numbers, 1 = props";

  n_length_bins_ = model_->number_of_length_bins();
  model_len_bins_ = n_length_bins_;
  if (sexed_flag_)
    n_length_bins_ *= 2;
  target_length_distribution_.resize(n_length_bins_);

}

/**
 *
 */
void MortalityEventBiomassLengthClusters::DoBuild() {
  LOG_MEDIUM();
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
  // can differ from model years
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

  if(sexed_flag_) {
    stratum_lf_.resize(n_length_bins_ * 2 , 0.0);
  } else {
    stratum_lf_.resize(n_length_bins_ ,  0.0);
  }
  agent_ndx_for_age_subsample_within_cluster_.resize(length_samples_per_clusters_, 0);
  length_bins_ = model_->length_bins();

  cluster_weight_.resize(years_.size());
  cluster_length_sample_weight_.resize(years_.size());
  cluster_length_samples_.resize(years_.size());
  cluster_length_samples_sex_.resize(years_.size());
  agent_ndx_cluster_.resize(length_samples_per_clusters_, 0);
  length_target_.resize(years_.size());

  for(unsigned i = 0; i < years_.size(); ++i) {
    cluster_length_samples_[i].resize(cells_.size());
    cluster_length_samples_sex_[i].resize(cells_.size());
    cluster_length_sample_weight_[i].resize(cells_.size());
    cluster_weight_[i].resize(cells_.size());
    length_target_[i].resize(cells_.size());
    for(unsigned j = 0; j < cells_.size(); ++j) {
      cluster_length_samples_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]]);
      cluster_length_samples_sex_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]]);
      cluster_length_sample_weight_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]],0.0);
      cluster_weight_[i][j].resize(cluster_by_year_and_stratum_[years_[i]][cells_[j]],0.0);
      length_target_[i][j].resize(n_length_bins_, 0.0);
      for(unsigned k = 0; k < cluster_by_year_and_stratum_[years_[i]][cells_[j]]; ++k) {
        cluster_length_samples_[i][j][k].resize(length_samples_per_clusters_, 0);
        cluster_length_samples_sex_[i][j][k].resize(length_samples_per_clusters_, 0);
      }
    }
  }



}

/**
 *
 */
void MortalityEventBiomassLengthClusters::PreExecute() {

}

/**
 *
 */
void MortalityEventBiomassLengthClusters::Execute() {

}
/**
 *  Reset dynamic containers, in between simulations
 */
void MortalityEventBiomassLengthClusters::ResetPreSimulation() {
  LOG_FINE() << "ResetPreSimulation";
  for(unsigned i = 0; i < years_.size(); ++i) {
    for(unsigned j = 0; j < cells_.size(); ++j) {
      fill(cluster_weight_[i][j].begin(), cluster_weight_[i][j].end(),0.0);
      fill(length_target_[i][j].begin(), length_target_[i][j].end(),0.0);
      for(unsigned k = 0; k < cluster_by_year_and_stratum_[years_[i]][cells_[j]]; ++k) {
        fill(cluster_length_samples_[i][j][k].begin(), cluster_length_samples_[i][j][k].end(),0);
        fill(cluster_length_samples_sex_[i][j][k].begin(), cluster_length_samples_sex_[i][j][k].end(),0);
      }
    }
  }
  ClearComparison(); // Clear comparisons
  LOG_FINE() << "Exit: ResetPreSimulation";
}

/**
 *
 */
void MortalityEventBiomassLengthClusters::Simulate() {
  unsigned i = 0;
  LOG_MEDIUM() << "Simulating data for observation = " << label_ << " about to get fishery label = " << fishery_label_;
  if (model_->run_mode() != (RunMode::Type)(RunMode::kMSE))
    ResetPreSimulation();
  utilities::RandomNumberGenerator& rng = utilities::RandomNumberGenerator::Instance();
  vector<vector<processes::census_data>> fishery_age_data = mortality_process_->get_fishery_census_data(fishery_label_);

  if (fishery_age_data.size() == 0) {
    LOG_CODE_ERROR() << "could not return information for fishery " << fishery_label_ << " this error should be dealt with earlier in the code";
  }

  //vector<processes::composition_data>& length_frequency = mortality_process_->get_removals_by_length();
  LOG_MEDIUM() << "length of census data (years * cells) = " << fishery_age_data.size();

  vector<unsigned> census_stratum_ndx;

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
    LOG_MEDIUM() << "About to sort our info for year " << years_[year_ndx] << " fishery index " << fishery_year_ndx;

    vector<processes::census_data>& fishery_year_census = fishery_age_data[fishery_year_ndx];
    for (unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      stratum_length_frequency_[cells_[stratum_ndx]].resize(n_length_bins_, 0.0);
      LOG_MEDIUM() << "About to sort our info for stratum " << cells_[stratum_ndx];
      unsigned clusters_to_sampled = cluster_by_year_and_stratum_[years_[year_ndx]][cells_[stratum_ndx]];
      census_ndx_cluster_.resize(clusters_to_sampled);
      for(i = 0; i < clusters_to_sampled; ++i) {
        census_ndx_cluster_[i].clear();
      }

      LOG_MEDIUM() << "clusters to sample " << clusters_to_sampled << " - " << census_ndx_cluster_.size();
      census_stratum_ndx.clear();
      //unsigned total_agents_in_ALK = 0;
      // Reset Stratum Objects
      stratum_biomass_[cells_[stratum_ndx]] = 0.0;
      fill(stratum_lf_.begin(),stratum_lf_.end(), 0.0);
      fill(target_length_distribution_.begin(),target_length_distribution_.end(), 0);

      // -- Find which census objects relate to this year and stratum
      //    save that information to do a look up later.
      // -- if more than one cell in stratum find biomass weights by sex
      unsigned census_ndx = 0; // links back to the census
      LOG_MEDIUM() << "number of cells in this year and fishery = " << fishery_year_census.size();
      float total_stratum_biomass = 0.0;
      vector<float> biomass_by_cell;
      for (processes::census_data& census : fishery_year_census) {
        // Find census elements that are in this stratum
        if ((find(stratum_rows_[cells_[stratum_ndx]].begin(),stratum_rows_[cells_[stratum_ndx]].end(), census.row_) != stratum_rows_[cells_[stratum_ndx]].end()) && (find(stratum_cols_[cells_[stratum_ndx]].begin(),stratum_cols_[cells_[stratum_ndx]].end(), census.col_) != stratum_cols_[cells_[stratum_ndx]].end())) {
          LOG_MEDIUM() << "census element = " << census_ndx << " agents in this cell = " << census.length_ndx_.size() << " row = " << census.row_ << " col  = " << census.col_ << " year = " << census.year_ << " biomass = " << census.biomass_;
          if (census.length_ndx_.size() > 0) {
            //LOG_FINE() << "found a census that year and cell work, agents in it = " << census.age_ndx_.size() << " proportion to take = " << clusters_to_sampled;
            census_stratum_ndx.push_back(census_ndx);
            // Calculate the agents available in the first sampling unit
            LOG_MEDIUM() << "agents in this cell = " << census.length_ndx_.size() << " row = " << census.row_ << " col  = " << census.col_ << " year = " << census.year_;

            /*
             * Target distribution for the both sexed and unsexed models is unsexed!! this is not ideal, start with this though
             */
            total_stratum_biomass += census.biomass_ + census.female_biomass_;
            biomass_by_cell.push_back(census.biomass_ + census.female_biomass_);
            // Stratum length target distribution
            if (sexed_flag_) {
              for (i = 0; i < census.length_ndx_.size(); ++i) {
                if (census.sex_[i] == 0) {
                  target_length_distribution_[census.length_ndx_[i]]++;
                } else {
                  target_length_distribution_[census.length_ndx_[i] + model_len_bins_]++;
                }
              }
            } else {
              for (i = 0; i < census.length_ndx_.size(); ++i)
                target_length_distribution_[census.length_ndx_[i]]++;
            }
          }
        }
        ++census_ndx;
      }
      LOG_MEDIUM() << "census objects in this stratum = " << census_stratum_ndx.size();


      // Turn target AF's into proportions
      float tot_af = 0;
      for (i =0; i < target_length_distribution_.size(); ++i)
        tot_af += target_length_distribution_[i];
      for (i =0; i < target_length_distribution_.size(); ++i) {
        target_length_distribution_[i] /= tot_af;
        LOG_FINE() << "target prop = " << target_length_distribution_[i];
        length_target_[year_ndx][stratum_ndx][i] = target_length_distribution_[i];
      }
      // If multiple cells in stratum then find out the distribution (proportion) of sampled clusters by cell
      // take samples of stratum proportional to total catch among cells e.g. if stratum A = cell 1 & cell 2 and we wanted
      // to take 100 samples in stratum A. If cell 1 had 80% catch, then we would take 80 samples from cell 1.
      vector<unsigned> clusters_to_sample_by_cell;
      for (auto & cell_bio : biomass_by_cell) {
        cell_bio /= total_stratum_biomass;
        LOG_MEDIUM() << "prop of clusters to sample from this cell = " << cell_bio;
        clusters_to_sample_by_cell.push_back(cell_bio * clusters_to_sampled);
      }

      // -------------------
      // For each stratum
      // -------------------
      float cluster_size = 0;
      unsigned cluster_size_numbers = 0;
      vector<unsigned> expected_cluster_numbers;
      vector<float> prob_cluster_numbers;
      unsigned agents_available = 1000;
      unsigned agent_ndx = 0, len_bin_ndx = 0, agent_counter = 0, temp_agent_ndx = 0;
      float ratio = 0;
      float mean_weight_of_agents = 0.0;
      unsigned total_cluster_ndx = 0;
      for (unsigned cell_ndx = 0; cell_ndx < census_stratum_ndx.size(); ++cell_ndx) {
        unsigned clusters_to_collate = clusters_to_sample_by_cell[cell_ndx];
        processes::census_data& census = fishery_year_census[census_stratum_ndx[cell_ndx]];
        LOG_MEDIUM() << "Biomass available in this cell = " << census.biomass_;
        agents_available = census.length_ndx_.size();
        // calculate mean weight for each cell, we are going to randomly draw a sampling unit (landing/tow) which is weight based
        // Then based on characteristics of clusters, we convert this to numbers using this 'mean_weight_of_agents' value
        mean_weight_of_agents = census.biomass_ / math::Sum(census.scalar_);
        LOG_MEDIUM() << "number of individuals = " << math::Sum(census.scalar_) << " number of agents = " << agents_available;
        //unsigned agent_counter = 0;
        cluster_size = 0.0;
        // -------------------
        // Generate clusters
        // For each clusters
        // Calculate Target distribution using M-H sampling algorithm
        // -------------------
        for (unsigned cluster_ndx = 0; cluster_ndx < clusters_to_collate; ++cluster_ndx, ++total_cluster_ndx) {
          // Weight of this cluster
          cluster_size = rng.lognormal(average_cluster_weight_, cluster_cv_);
          fill(agent_ndx_cluster_.begin(),agent_ndx_cluster_.end(), 0);
          cluster_weight_[year_ndx][stratum_ndx][cluster_ndx] = cluster_size;
          //cluster_length_sample_weight_[year_ndx][stratum_ndx][cluster_ndx] = cluster_size;

          cluster_size_numbers = (unsigned)(cluster_size / mean_weight_of_agents); // turn to numbers
          LOG_MEDIUM() << "cluster ndx = " << total_cluster_ndx << " cluster size = " << cluster_size << " average cluster weight " << average_cluster_weight_ << " numbers = " << cluster_size_numbers << " mean weight of agents = " << mean_weight_of_agents;

          // Randomly draw the starting value for the cluster and M-H algorithm
          agent_ndx = agents_available * rng.chance();
          agent_ndx_cluster_[0] = agent_ndx;

          census_ndx_cluster_[total_cluster_ndx].push_back(census_stratum_ndx[cell_ndx]);

          cluster_length_samples_[year_ndx][stratum_ndx][total_cluster_ndx][0] = census.length_ndx_[agent_ndx];
          cluster_length_samples_sex_[year_ndx][stratum_ndx][total_cluster_ndx][0] = census.sex_[agent_ndx];

          unsigned y = census.length_ndx_[agent_ndx];
          unsigned y_proposed = 0;
          LOG_FINE() << "y = " << y;
          unsigned proposal_attemps = 100;
          float val = 0.0;
          // Start the M-H sampling algorithm
          for (unsigned j = 1; j < length_samples_per_clusters_; ++j) {
            LOG_FINE() << "j = " << j;
            proposal_attemps = 100;
            // randomly draw the length sample that is within the ranges of target distribution
            while(proposal_attemps > 0) {
              proposal_attemps--;
              val = rng.normal(y, cluster_sigma_);
              if ((val >= 0) & (val < n_length_bins_)) {
                y_proposed = (unsigned)roundf(val);
                break;
              }
            }
            if (proposal_attemps < 0)
              y_proposed = y;

            LOG_FINE() << "proposed = " << y_proposed << " val = " <<  val << " y = " << y << " proosal attempts = " << proposal_attemps;

            // calculate the acceptance ratio
            ratio = target_length_distribution_[y_proposed] / target_length_distribution_[y];

            LOG_FINE() << "proposed = " << y_proposed << " val = " <<  val << " y = " << y;
            LOG_FINE() << "proposed = " << y_proposed << " target " <<  target_length_distribution_[y_proposed] << " val = " << val << " y = " << y << " target = " << target_length_distribution_[y] << " ratio = " << ratio;
            // accept or reject this jump
            if (rng.chance() <= ratio) {
              // accept this jump so find a random element with this length to save attributes
              // update y to proposal for next jump after this
              y = y_proposed;
              agent_counter = 10000;
              while(agent_counter > 0) {
                LOG_FINE() << "attemtp = " << agent_counter;
                --agent_counter;
                temp_agent_ndx = agents_available * rng.chance();
                if (census.length_ndx_[temp_agent_ndx] == y_proposed) {
                  // have we sampled this agent before
                  if (find(agent_ndx_cluster_.begin(), agent_ndx_cluster_.end(), temp_agent_ndx) != agent_ndx_cluster_.end())
                    continue;
                  agent_ndx_cluster_[j] = temp_agent_ndx;
                  cluster_length_samples_[year_ndx][stratum_ndx][total_cluster_ndx][j] = census.length_ndx_[temp_agent_ndx];
                  cluster_length_sample_weight_[year_ndx][stratum_ndx][total_cluster_ndx] += census.scalar_[temp_agent_ndx] * census.weight_[temp_agent_ndx];
                  cluster_length_samples_sex_[year_ndx][stratum_ndx][total_cluster_ndx][j] = census.sex_[temp_agent_ndx];
                  break; // while loop
                }
              }
              /*
               * If we can't attribute using the random selection systematically sample to find this attribute
               */
              if (agent_counter == 0) {
                LOG_FINE() << "need to systematically sample";
                for (i = 0; i < census.length_ndx_.size(); ++i) {
                  if (i == (census.length_ndx_.size() - 1))
                    agent_ndx_cluster_.clear();

                  // Check we haven't already sampled this agent.
                  if (census.length_ndx_[i] == y_proposed) {
                    if (find(agent_ndx_cluster_.begin(), agent_ndx_cluster_.end(), i) != agent_ndx_cluster_.end())
                      continue;
                    LOG_FINEST() << "rejecting jump resampling a similar agent as the last " << y << " choosing agent at index = " << i;
                    agent_ndx_cluster_[j] = i;
                    cluster_length_samples_[year_ndx][stratum_ndx][total_cluster_ndx][j] = census.length_ndx_[i];
                    cluster_length_sample_weight_[year_ndx][stratum_ndx][total_cluster_ndx] += census.scalar_[i] * census.weight_[i];
                    cluster_length_samples_sex_[year_ndx][stratum_ndx][total_cluster_ndx][j] = census.sex_[i];
                    break;
                  }
                }
              }
            } else {
              LOG_FINE() << "reject proposal";
              // Failed to accept proposal so find an agent with the previous attribute and y stays as y
              agent_counter = 10000;
              while(agent_counter > 0) {
                --agent_counter;
                temp_agent_ndx = agents_available * rng.chance();
                if (census.length_ndx_[temp_agent_ndx] == y) {
                  // have we sampled this agent before
                  if (find(agent_ndx_cluster_.begin(), agent_ndx_cluster_.end(), temp_agent_ndx) != agent_ndx_cluster_.end())
                    continue;
                  agent_ndx_cluster_[j] = temp_agent_ndx;
                  cluster_length_samples_[year_ndx][stratum_ndx][total_cluster_ndx][j] = census.length_ndx_[temp_agent_ndx];
                  cluster_length_sample_weight_[year_ndx][stratum_ndx][total_cluster_ndx] += census.scalar_[temp_agent_ndx] * census.weight_[temp_agent_ndx];
                  cluster_length_samples_sex_[year_ndx][stratum_ndx][total_cluster_ndx][j] = census.sex_[temp_agent_ndx];
                  break; // while loop
                }
              }
              /*
               * If we can't attribute using the random selection systematically sample to find this attribute
               */
              if (agent_counter == 0) {
                LOG_FINE() << "need to systematically sample";
                for (i = 0; i < census.length_ndx_.size(); ++i) {
                  if (i == (census.length_ndx_.size() - 1))
                    agent_ndx_cluster_.clear();

                  // Check we haven't already sampled this agent.
                  if (census.length_ndx_[i] == y) {
                    if (find(agent_ndx_cluster_.begin(), agent_ndx_cluster_.end(), i) != agent_ndx_cluster_.end())
                      continue;
                    LOG_MEDIUM() << "rejecting jump resampling a similar agent as the last " << y << " choosing agent at index = " << i;
                    agent_ndx_cluster_[j] = i;
                    cluster_length_samples_[year_ndx][stratum_ndx][total_cluster_ndx][j] = census.length_ndx_[i];
                    cluster_length_sample_weight_[year_ndx][stratum_ndx][total_cluster_ndx] += census.scalar_[i] * census.weight_[i];
                    cluster_length_samples_sex_[year_ndx][stratum_ndx][total_cluster_ndx][j] = census.sex_[i];
                    break;
                  }
                }
              }
            }
          } // age samples
          LOG_FINE() << "finishing cluster MH algorithm";
          float scalar = cluster_weight_[year_ndx][stratum_ndx][total_cluster_ndx] / cluster_length_sample_weight_[year_ndx][stratum_ndx][total_cluster_ndx];
          LOG_FINE() << "scalar = " << scalar << " total weight = " << cluster_weight_[year_ndx][stratum_ndx][total_cluster_ndx] << " sampled weight = " << cluster_length_sample_weight_[year_ndx][stratum_ndx][total_cluster_ndx];
          for (unsigned j = 0; j < length_samples_per_clusters_; ++j) {
            len_bin_ndx = cluster_length_samples_[year_ndx][stratum_ndx][total_cluster_ndx][j];
            LOG_FINE() << "j = " << j << " len bin ndx = " << len_bin_ndx << " scalar = " << scalar;
            if (sexed_flag_) {
              stratum_lf_[len_bin_ndx + (model_len_bins_ * cluster_length_samples_sex_[year_ndx][stratum_ndx][total_cluster_ndx][len_bin_ndx])] += scalar;
            } else
              stratum_lf_[len_bin_ndx] += scalar;
          }
        } // Cluster_ndx
      } // cells
      // Save AF
      if (sexed_flag_) {
        LOG_FINE() << "size of AG = " << stratum_lf_.size() ;
        for(unsigned sex_ndx = 0; sex_ndx <= 1; sex_ndx++) {
          for (unsigned len_ndx = 0; len_ndx < model_len_bins_; ++len_ndx) {
            SaveComparison(0, sex_ndx, model_->length_bin_mid_points()[len_ndx], cells_[stratum_ndx], stratum_lf_[len_ndx + (sex_ndx * model_len_bins_)], 0.0, 0, years_[year_ndx]);
          }
        }
      } else {
        for (unsigned len_ndx = 0; len_ndx < model_len_bins_; ++len_ndx) {
          LOG_FINE() << "numbers at length while saveing = " << stratum_lf_[len_ndx];
          SaveComparison(0, 0, model_->length_bin_mid_points()[len_ndx], cells_[stratum_ndx], stratum_lf_[len_ndx], 0.0, 0, years_[year_ndx]);
        }
      }
    } // Stratum loop
  } // year loop
  for (auto& iter : comparisons_) {
    for (auto& second_iter : iter.second) {  // cell
      float total = 0.0;
      if (are_obs_props_) {
        for (auto& comparison : second_iter.second)
          total += comparison.expected_;
        for (auto& comparison : second_iter.second)
          comparison.expected_ /= total;
      }
      // No simulation in this, simulated = expected
      for (auto& comparison : second_iter.second) {
        comparison.simulated_ = comparison.expected_;
      }
    }
  }
} // Simulate



void MortalityEventBiomassLengthClusters::FillReportCache(ostringstream& cache) {
  LOG_FINE() << "we are here";
  // Print the age length key for curiosity
  /*
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
*/

  // Cluster weight
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    cache << "cluster_biomass-" << years_[year_ndx] << " "  << REPORT_R_MATRIX <<"\n";
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_weight_[year_ndx][stratum_ndx].size(); ++cluster_ndx)
        cache << cluster_weight_[year_ndx][stratum_ndx][cluster_ndx] << " ";
      cache << "\n";
    }
  }

  // Cluster length  sample
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_length_samples_[year_ndx][stratum_ndx].size(); ++cluster_ndx) {
        cache << "cluster_length_sample-" << years_[year_ndx] << "-" << cells_[stratum_ndx] << "-" << cluster_ndx+1 << ": ";
        //cache << cluster_ndx + 1 << " ";
        for (auto& cluster_val : cluster_length_samples_[year_ndx][stratum_ndx][cluster_ndx])
          cache <<  model_->length_bin_mid_points()[cluster_val] << " ";
        cache << "\n";
      }
    }
  }
  /*
  // Cluster sex
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    cache << "cluster_sex-" << years_[year_ndx] << " " << REPORT_R_MATRIX <<"\n";
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_age_samples_sex_[year_ndx][stratum_ndx].size(); ++cluster_ndx)
        cache << cluster_age_samples_sex_[year_ndx][stratum_ndx][cluster_ndx] << " ";
      cache << "\n";
    }
  }

*/

  // Target length distribution
  for (unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    cache << "target_length-" << years_[year_ndx] << " " << REPORT_R_DATAFRAME << "\n";
    for (unsigned len_bin = 0; len_bin <  model_->length_bin_mid_points().size(); ++len_bin)
      cache << model_->length_bin_mid_points()[len_bin] << " ";
    cache << "\n";
    for (unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      //cache << cluster_ndx + 1 << " ";
      for (auto& cluster_val : length_target_[year_ndx][stratum_ndx])
        cache << cluster_val << " ";
      cache << "\n";
    }
  }


/*



  // Cluster Census
  for(unsigned year_ndx = 0; year_ndx < years_.size(); ++year_ndx) {
    for(unsigned stratum_ndx = 0; stratum_ndx < cells_.size(); ++stratum_ndx) {
      for(unsigned cluster_ndx = 0; cluster_ndx < cluster_census_[year_ndx][stratum_ndx].size(); ++cluster_ndx) {
        cache << "cluster_census_age-"<< years_[year_ndx] << "-" << cells_[stratum_ndx] << "-" << cluster_ndx + 1 <<  ": ";
        for (auto& cluster_val : cluster_census_[year_ndx][stratum_ndx][cluster_ndx])
          cache << cluster_val + model_->min_age() << " ";
        cache << "\n";
      }
    }
  }

*/



}

} /* namespace observations */
} /* namespace niwa */

