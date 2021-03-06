/**
 * @file TagRecaptureByAge.h
 * @author  C.Marsh github.com/Craig44
 * @date 4/11/2018
 * @section LICENSE
 *
 *
 * @section DESCRIPTION
 *
 * This class takes tag-racapture object from mortality which, is responsible for undertaking recapture, and bundles it up so that we can simulated
 * many permutations to identigy the effect of trap shyness and other factors that generate overdispersion, or disrupt conventional assumptions
 */
#ifndef OBSERVATIONS_TAG_RECAPTURE_BY_AGE_H_
#define OBSERVATIONS_TAG_RECAPTURE_BY_AGE_H_

// headers
#include "Observations/Observation.h"
#include "Layers/Children/CategoricalLayer.h"

#include "AgeingErrors/AgeingError.h"
#include "Processes/Children/Mortality.h"


// namespaces
namespace niwa {
namespace observations {

using processes::Mortality;



/**
 * class definition
 */
class TagRecaptureByAge : public niwa::Observation {
public:
  // methods
  TagRecaptureByAge(Model* model);
  virtual                     ~TagRecaptureByAge();
  void                        DoValidate() override final;
  virtual void                DoBuild() override;
  void                        DoReset() override final { };
  void                        PreExecute() override final;
  void                        Execute() override final;
  void                        Simulate() override final;
  bool                        HasYear(unsigned year) const override final { return std::find(years_.begin(), years_.end(), year) != years_.end(); }
  virtual void                FillReportCache(ostringstream& cache);

protected:
  // members
  vector<unsigned>                years_;
  vector<string>                  recapture_stratum_;
  string                          release_stratum_;

  layers::CategoricalLayer*       layer_ = nullptr;
  string                          layer_label_;
  AgeingError*                    ageing_error_ = nullptr;
  string                          ageing_error_label_;
  vector<unsigned>        				age_freq_;
  vector<float>                   scanned_age_freq_;
  unsigned					              tag_release_year_;
  unsigned                        number_of_bootstraps_ = 0;
  bool                            sexed_ = false; // TODO add this to the constructor

  Mortality*                      mortality_process_ = nullptr;
  string                          process_label_;
  vector<processes::tag_recapture>* tag_recapture_data_ = nullptr;

  vector<unsigned>                release_stratum_rows_;
  vector<unsigned>                release_stratum_cols_;
  map<string,vector<unsigned>>    recapture_stratum_rows_;
  map<string,vector<unsigned>>    recapture_stratum_cols_;
  WorldView*                      world_ = nullptr;
  
};

} /* namespace observations */
} /* namespace niwa */

#endif /* OBSERVATIONS_TAG_RECAPTURE_BY_AGE_H_ */
