/**
 * @file WorldView.h
 * @author C.Marsh
 * @github https://github.com/Craig44
 * @date 12/07/2018
 * @section LICENSE
 *
 * @section DESCRIPTION
 *
 *    Following SPM - The world maintains a Grid (base_grid_) of WorldCell objects. This
 *    represents the current state of the world as it is now. A second
 *    grid called the Drifference Grid (cached_grid_) is an exact copy
 *    of pGrid in dimensions, but it maintains only the difference
 *    (adjustments) we need to make to the world. After each process is run
 *    the difference grid is merged into the world grid to update the current
 *    state.
 *
 *    The runMode calls allow the application entry (int main()) easy access
 *    to call functions based on each run mode. The World then sets up what
 *    is required to kick-off each type of run.
 *
 *    Note: CWorld is a singleton because we only want one ever. Multiple
 *    worlds are not prohibited.

 */
#ifndef WORLD_H_
#define WORLD_H_

// headers
#include <map>
#include <list>
#include <vector>
#include <string>

#include "Layers/Children/NumericLayer.h"
// namespaces
namespace niwa {

class Model;
class WorldCell;
class NumericLayer;

using std::string;
/**
 * Class definition
 */
class WorldView {

public:
  // methods
  WorldView(Model* model);
  virtual                     ~WorldView();
  void                        Validate();
  void                        Build();
  void                        Reset();

  // Accessors
  float                      get_abundance(void) {return 1.0; };
  float                      get_biomass(void) {return 1.0; };
  //unsigned                    get_height() { return height_; }  // these can be called off the model
  //unsigned                    get_width() { return width_; }  // these can be called off the model

  //TODO
  WorldCell*                  get_base_square(int RowIndex, int ColIndex);
  WorldCell*                  get_cached_square(int RowIndex, int ColIndex);
  unsigned                    get_enabled_cells() {return enabled_cells_; };
  vector<unsigned>            get_enabled_rows() {return enabled_rows_; };
  vector<unsigned>            get_enabled_cols() {return enabled_cols_; };

  void                        CachedWorldForInit();
  void                        MergeWorldForInit();
  void                        MergeCachedGrid();
  void                        MergeCachedGrid(bool update_lat_long);
  void                        get_world_age_frequency(vector<float>& world_age_freq);
  void                        get_cell_element(unsigned& row, unsigned& col, const float lat, const float lon);
  // Used by methods to tell the world that time varying parameters need to be changed, usually in the RebuildCache() function of a process
  void                        rebuild_growth_params() {update_growth_params_ = true;}
  void                        rebuild_mort_params() {update_mortality_params_ = true;}
  //Called by the Model in RunBasic after time varying to update agent params if need be
  void                        rebuild_agent_time_varying_params();

protected:
  // members
  WorldCell                   **base_grid_;
  WorldCell                   **cached_grid_;
  WorldCell                   **init_cached_grid_;
  niwa::layers::NumericLayer*     base_layer_ = nullptr;
  unsigned                    width_;
  unsigned                    height_;
  unsigned                    enabled_cells_;
  vector<unsigned>            enabled_rows_;
  vector<unsigned>            enabled_cols_;
  vector<float>               lat_midpoint_by_cell_;
  vector<float>               lon_midpoint_by_cell_;
  vector<float>               lat_bounds_;
  vector<float>               lon_bounds_;
  bool                        update_growth_params_ = false;
  bool                        update_mortality_params_ = false;

private:
  // members
  Model*                      model_ = nullptr;


};
} /* namespace niwa */

#endif /* WORLD_H_ */
