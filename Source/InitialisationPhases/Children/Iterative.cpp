/**
 * @file Iterative.cpp
 * @author  C.Marsh
 * @date 12/07/2018
 * @section LICENSE
 *
 *
 */
// headers
#include "Iterative.h"

#include <algorithm>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/algorithm/string/split.hpp>


#include "Processes/Manager.h"
#include "TimeSteps/Factory.h"
#include "TimeSteps/Manager.h"
#include "Layers/Manager.h"
#include "Processes/Manager.h"
#include "Reports/Manager.h"
#include "Model/Model.h"
#include "World/WorldCell.h"
// namespaces
namespace niwa {
namespace initialisationphases {

/**
 * Default constructor
 */
Iterative::Iterative(Model* model)
  : InitialisationPhase(model) {
  parameters_.Bind<unsigned>(PARAM_YEARS, &years_, "The number of iterations (years) over which to execute this initialisation phase", "");
  parameters_.Bind<unsigned>(PARAM_INITIAL_NUMBER_OF_AGENTS, &number_agents_, "The number of agents to initially seed in the partition", "");
  parameters_.Bind<string>(PARAM_LAYER_LABEL, &intial_layer_label_, "The label of a layer that you want to seed a distribution by.", "", "");
  parameters_.Bind<string>(PARAM_RECRUITMENT_LAYER_LABEL, &recruitement_layer_label_, "The label of a layer has a recruitment process label in each cell to see how to set scalars", "", "");
  parameters_.Bind<float>(PARAM_INIT_MORTALITY_RATE, &shortcut_m_, "The instaneous mortality rate to use to approximate a crude initial age-structure", "", 0.2);
}

/**
 *
 */
void Iterative::DoValidate() {
  LOG_TRACE();
}

/**
 *
 */
void Iterative::DoBuild() {
  LOG_TRACE();

  // Did the user supply a layer
  if (intial_layer_label_ != "") {
    initial_layer_ = model_->managers().layer()->GetNumericLayer(intial_layer_label_);
    if (!initial_layer_)
      LOG_ERROR_P(PARAM_LAYER_LABEL) << "could not find layer does it exist? if it does exist, is of type numeric? because it needs to be";

    if (!initial_layer_->is_proportion()) {
      LOG_ERROR_P(PARAM_LAYER_LABEL) << "can you please make sure that the layer has subcommand proportion = true, this means the layer class will check if the layer is acceptable for this class so I don't have to duplicate code. THanks =)";
    }
  }

  // Did the user supply a layer
  if (parameters_.Get(PARAM_RECRUITMENT_LAYER_LABEL)->has_been_defined()) {
    recruitement_layer_ = model_->managers().layer()->GetCategoricalLayer(recruitement_layer_label_);
    if (!recruitement_layer_)
      LOG_ERROR_P(PARAM_RECRUITMENT_LAYER_LABEL) << "could not find the layer " << recruitement_layer_label_ << " does it exist? if it does exist, is of type numeric? because it needs to be";
  }
  // Check the entries
  auto recruit_processes = model_->managers().process()->GetRecruitmentProcesses();
  if (!parameters_.Get(PARAM_RECRUITMENT_LAYER_LABEL)->has_been_defined()) {
    if (recruit_processes.size() != 1)
      LOG_FATAL()<< "In the initialisation process " << label_ << " you need to supply the parameter "<< PARAM_RECRUITMENT_LAYER_LABEL << " if there is more that one recruitment process, we found " << recruit_processes.size() << " please include this layer";
    recruitment_label_for_single_cases_ = recruit_processes[0]->label();
    single_recruitment_case_ = true;
  } else {
    vector<string> recruit_labels;
    for(auto recruit_process : recruit_processes)
      recruit_labels.push_back(recruit_process->label());
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      for (unsigned col = 0; col < model_->get_width(); ++col) {
        LOG_FINEST() << "in row = " << row + 1 << " col = " << col + 1 << " recruit label = " << recruitement_layer_->get_value(row,col);
        if (find(recruit_labels.begin(), recruit_labels.end(), recruitement_layer_->get_value(row,col)) == recruit_labels.end())
          LOG_ERROR_P(PARAM_RECRUITMENT_LAYER_LABEL) << "could not find the recruitment process = " << recruitement_layer_->get_value(row,col) << " check the process exists or that there is not a typo.";
      }
    }
  }


  // Generate a pointer to the world
  world_ = model_->world_view();
  if (!world_)
    LOG_CODE_ERROR() << "could not create the world via reference, something is wrong";

  time_steps_ = model_->managers().time_step()->ordered_time_steps();


  model_->set_n_agents(number_agents_);
}

/**
 * Execute our iterative initialisation phases.
 */
void Iterative::Execute() {
  LOG_TRACE();
  LOG_MEDIUM();
  // Check the recruitment labels in the recruitment layer make sense

  // Check we get a sensible estimate of M
  if (!parameters_.Get(PARAM_INIT_MORTALITY_RATE)->has_been_defined()) {
    if (model_->get_m() <= 0.0)
      LOG_FATAL() << "Could not get a sensible M value from the mortality process defined this is unusual you want to check everything is okay in the mortality process or contact a developer";
  }
  // Calculate the R0 values for the recruitment processes this is a bit crude but will do for now
  unsigned large_age = model_->max_age() * 4;
  //unsigned large_age = 200;
  float number = 0; // this will be approximate number of equilibrium individuals in the system
  float m = 0.2;
  if (!parameters_.Get(PARAM_INIT_MORTALITY_RATE)->has_been_defined())
    m = model_->get_m(); //TODO put this at line 122 and give it a cell row and col call and return M if spatially variable  This is important to set, otherwise age distribution will get all weird
  else
    m = shortcut_m_;

  // In order to seed the approximate number of agents given by number_agents_ we need to know approximately how many indiviudals are in the system.
  vector<string> recruitment_labels;
  float total_r0 = 0.0;
  for (auto iter : model_->get_r0s()) {
    recruitment_labels.push_back(iter.first);
    total_r0 += iter.second;
    for (unsigned age = 0; age < large_age; ++age)
      number +=  iter.second * exp(- m * age);
  }
  float global_scalar = number / number_agents_;
  LOG_FINE() << "total R0 = " << total_r0 << " number = " << number << " number of agents = " << number_agents_ << "scalar = " << global_scalar;

  if (global_scalar < 1.0) {
    LOG_WARNING() << "the scalar that compares your individual numbers to population numbers is less than 1.0 (" << global_scalar << "), this means that given your growth model and number of individuals you are modelling more individuals than could/should exist. I think you could get away with modelling less individuals.";
  }

  for (auto iter : model_->get_r0s())  {
    model_->set_scalar(iter.first, global_scalar);
    model_->set_r0_agents(iter.first, (unsigned)(iter.second / global_scalar));
  }
 
  // Move on and seed our n_agents
  unsigned cells = world_->get_enabled_cells();
  vector<vector<int>> agents_per_cell(model_->get_height());
  if (intial_layer_label_ == "") {
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      agents_per_cell[row].resize(model_->get_width(),0.0);
      for (unsigned col = 0; col < model_->get_width(); ++col) {
        agents_per_cell[row][col] = (int)number_agents_ / (int)cells;
        LOG_FINEST() << "number of cells = " << cells << " number of agents per cell = " << agents_per_cell[row][col];
      }
    }
  } else {
    for (unsigned row = 0; row < model_->get_height(); ++row) {
      agents_per_cell[row].resize(model_->get_width(),0.0);
      for (unsigned col = 0; col < model_->get_width(); ++col) {
        LOG_FINE() << "row = " << row + 1 << " col = " << col + 1 << " number of agents = " << (int)number_agents_;
        LOG_FINE() << "initial_layer value = " << initial_layer_->get_value(row,col) << " value = " << (int)(number_agents_ * initial_layer_->get_value(row,col));;
        agents_per_cell[row][col] = (int)(number_agents_ * initial_layer_->get_value(row,col));
        LOG_FINE() << "number of possible agents per cell = " << agents_per_cell[row][col];
      }
    }
  }
  // float seed_z = model_->get_m();
  unsigned init_year = model_->start_year() - years_ - 1;
  model_->set_current_year_in_initialisation(init_year);

  LOG_FINE() << "check seed = " << m;
  // Seed some individuals in the world
  for (unsigned row = 0; row < model_->get_height(); ++row) {
    for (unsigned col = 0; col < model_->get_width(); ++col) {
      WorldCell* cell = world_->get_base_square(row, col);
      if (cell->is_enabled()) {
        cell->seed_agents(agents_per_cell[row][col], m, global_scalar);
        LOG_FINE() << "row " << row + 1 << " col = " << col + 1 << " seeded " << cell->agents_.size();
      }
    }
  }

  // Ask to print the intialisation phase here;
  model_->managers().report()->Execute(State::kInitialise);

  // Run out the initialisation phase
  timesteps::Manager& time_step_manager = *model_->managers().time_step();
  model_->set_current_year_in_initialisation(model_->start_year() - years_);

  // This iterates the annual cycle
  time_step_manager.ExecuteInitialisation(label_, years_);
  // I removed the convergence check because we seed agents by birth_year so we really need to have an incremental inital years to have a sensible age distribution
  // if we cut at 50 years when we thought we might be running for 100 years then this would cause an improper age distribution, So I leave it for the user to define
  // The correct burin in time to reach equilibrium

  // Set B0/SSBs
  float b0 = 0.0;
  for (auto iter : model_->get_r0s()) {
    b0 = model_->get_ssb(iter.first);
    model_->set_b0(iter.first, b0);
    LOG_FINE() << "stock = " << iter.first << " b0 = " <<  b0 << " R0 = " << iter.second;
    //model_->set_scalar(iter.first, scalar);
  }


  // Set scalar before continuing on
  LOG_MEDIUM();
}


} /* namespace initialisationphases */
} /* namespace niwa */
