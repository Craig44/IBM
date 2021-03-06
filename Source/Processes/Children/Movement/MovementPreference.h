/**
 * @file MovementPreference.h
 * @author C.Marsh
 * @github https://github.com/Craig44
 * @date 26/07/2018
 * @section LICENSE
 *
 *
 * @section DESCRIPTION
 * User defines a matrix of probabilities with a selectivity, that describes the probability of moving to another cell.
 *
 *
 */
#ifndef SOURCE_PROCESSES_CHILDREN_MOVEMENT_PREFERENCE_H_
#define SOURCE_PROCESSES_CHILDREN_MOVEMENT_PREFERENCE_H_

// headers
#include "Processes/Children/Movement.h"

#include "PreferenceFunctions/PreferenceFunction.h"
#include "Layers/Children/NumericLayer.h"

// namespaces
namespace niwa {
class Selectivity;
namespace processes {

/**
 * A movement struct that stores movement information
 */


/**
 * Class definition
 */
class MovementPreference : public Movement {
public:
  // methods
  explicit MovementPreference(Model* model);
  virtual                     ~MovementPreference() = default;
  void                        DoValidate() override final;
  void                        DoBuild() override final;
  void                        DoReset() override final { };
  void                        DoExecute() override final;
  void                        FillReportCache(ostringstream& cache) override final;
protected:
  // Methods
  void                        calculate_gradients();
  void                        calculate_diffusion_parameter(double& preference_value, double& standard_deviation);  // zeta and d_max and diffusion_paraemter_ are assumed to be available to this function

  //Members
  bool                        brownian_motion_;

  // Report containers
  double                       diffusion_parameter_;
  double                       standard_deviation_;
  double                       d_max_;
  double                       zeta_;
  double                       time_interval_;
  bool                        calculate_on_the_fly_ = false;
  vector<unsigned>            non_static_layer_ndx_;
  vector<string>              preference_function_labels_;
  vector<string>              preference_layer_labels_;

  vector<PreferenceFunction*> preference_functions_;
  vector<layers::NumericLayer*> preference_layers_;

  map<unsigned, vector<vector<double>>> meridonal_gradient_;
  map<unsigned, vector<vector<double>>> zonal_gradient_;
  map<unsigned, vector<vector<double>>> preference_by_year_;

  map<unsigned, vector<vector<double>>> average_meridonal_jump_;
  map<unsigned, vector<vector<double>>> average_zonal_jump_;

  vector<vector<double>>                initialisation_meridonal_gradient_;
  vector<vector<double>>                initialisation_zonal_gradient_;
  vector<vector<double>>                initialisation_preference_value_;

  // objects for thread safety of rng
  vector<float>                       lat_random_numbers_;
  vector<float>                       lon_random_numbers_;
  unsigned                            n_agents_;
  vector<vector<unsigned>>            cell_offset_;
  vector<string>                      selectivity_label_;
  vector<Selectivity*>                selectivity_;
  bool                                selectivity_length_based_;
  unsigned                            number_of_cells_gradient_ = 5;
  unsigned                            cell_offset_gradient_;

  bool                                print_individual_tracks_ = false;

};

} /* namespace processes */
} /* namespace niwa */

#endif /* SOURCE_PROCESSES_CHILDREN_MOVEMENT_PREFERENCE_H_ */
