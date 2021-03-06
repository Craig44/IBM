/**
 * @file Growth.cpp
 * @author C.Marsh
 * @github https://github.com/Craig44
 * @date 13/07/2018
 * @section LICENSE
 *
 *
 * @section DESCRIPTION
 *
 */

// headers
#include "Growth.h"

#include "Layers/Manager.h"
#include "Utilities/RandomNumberGenerator.h"
#include "TimeSteps/Manager.h"

// namespaces
namespace niwa {
namespace processes {

/**
 * constructor
 */
Growth::Growth(Model* model) : Process(model) {
  process_type_ = ProcessType::kGrowth;
  //parameters_.Bind<string>(PARAM_L_INF_LAYER_LABEL, &l_inf_layer_label_, "Label for the numeric layer that describes mean L_inf through space", ""); // Maybe add a cv by space?
  parameters_.Bind<float>(PARAM_TIME_STEP_PROPORTIONS, &time_step_proportions_, "A vector of proportions that describe how much of the annual increment to add to the lenght of an agent in each time step this process is applied", "");
  parameters_.Bind<string>(PARAM_DISTRIBUTION, &distribution_label_, "the distribution to allocate the parameters to the agents", "", PARAM_NORMAL)->set_allowed_values({PARAM_NORMAL, PARAM_LOGNORMAL});
  parameters_.Bind<bool>(PARAM_UPDATE_GROWTH_PARAMETERS, &update_growth_parameters_, "If an agent/individual moves do you want it to take on the growth parameters of the new spatial cell", "");
  parameters_.Bind<float>(PARAM_CV, &cv_, "The cv of the distribution", "");
}


void Growth::DoBuild() {
  LOG_FINE();

}

} /* namespace processes */
} /* namespace niwa */
