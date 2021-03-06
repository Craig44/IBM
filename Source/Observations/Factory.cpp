/**
 * @file Factory.cpp
 * @author  Scott Rasmussen (scott.rasmussen@zaita.com)
 * @version 1.0
 * @date 6/03/2013
 * @section LICENSE
 *
 * Copyright NIWA Science �2013 - www.niwa.co.nz
 *
 * $Date: 2008-03-04 16:33:32 +1300 (Tue, 04 Mar 2008) $
 */

// Headers
#include "Factory.h"

#include "Model/Model.h"
#include "Model/Managers.h"
#include "Observations/Manager.h"
#include "Observations/Children/AgeLength.h"
#include "Observations/Children/Biomass.h"
#include "Observations/Children/ProportionsAtAge.h"
#include "Observations/Children/ProcessRemovalsByAge.h"
#include "Observations/Children/ProcessRemovalsByLength.h"
#include "Observations/Children/MortalityScaledAgeFrequency.h"
#include "Observations/Children/MortalityEventBiomassScaledAgeFrequency.h"
#include "Observations/Children/MortalityEventBiomassClusters.h"
#include "Observations/Children/MortalityEventBiomassAgeClusters.h"
#include "Observations/Children/MortalityEventComposition.h"
#include "Observations/Children/MortalityEventBiomassLengthClusters.h"
#include "Observations/Children/TagRecaptureByLength.h"
#include "Observations/Children/TagRecaptureByAge.h"



// Namespaces
namespace niwa {
namespace observations {

/**
 * Create the instance of our object as defined by the two parameters
 * object_type and sub_type.
 *
 * @param object_type The type of object to create (e.g age_size, process)
 * @param sub_type The child type of the object to create (e.g ageing, schnute)
 * @return shared_ptr to the object we've created
 */
Observation* Factory::Create(Model* model, const string& object_type, const string& sub_type) {
  Observation* result = nullptr;
  if (object_type == PARAM_OBSERVATION) {

    if (sub_type == PARAM_PROCESS_REMOVALS_BY_AGE)
      result = new ProcessRemovalsByAge(model);
    if (sub_type == PARAM_MORTALITY_SCALES_AGE_FREQUENCY)
      result = new MortalityScaledAgeFrequency(model);
    if (sub_type == PARAM_MORTALITY_EVENT_BIOMASS_SCALED_AGE_FREQUENCY)
      result = new MortalityEventBiomassScaledAgeFrequency(model);
    if (sub_type == PARAM_MORTALITY_EVENT_BIOMASS_CLUSTERS)
      result = new MortalityEventBiomassClusters(model);
    if (sub_type == PARAM_MORTALITY_EVENT_BIOMASS_AGE_CLUSTERS)
      result = new MortalityEventBiomassAgeClusters(model);
    if (sub_type == PARAM_MORTALITY_EVENT_BIOMASS_LENGTH_CLUSTERS)
      result = new MortalityEventBiomassLengthClusters(model);
    if (sub_type == PARAM_MORTALITY_EVENT_COMPOSITION)
      result = new MortalityEventComposition(model);
    if (sub_type == PARAM_PROPORTIONS_AT_AGE)
      result = new ProportionsAtAge(model);
    else if (sub_type == PARAM_PROCESS_REMOVALS_BY_LENGTH)
      result = new ProcessRemovalsByLength(model);
    else if (sub_type == PARAM_BIOMASS)
      result = new Biomass(model);
    else if (sub_type == PARAM_AGE_LENGTH)
      result = new AgeLength(model);
    else if (sub_type == PARAM_TAG_RECAPTURE_BY_AGE)
      result = new TagRecaptureByAge(model);
    else if (sub_type == PARAM_TAG_RECAPTURE_BY_LENGTH)
      result = new TagRecaptureByLength(model);

    if (result)
      model->managers().observation()->AddObject(result);
  }
  return result;
}

} /* namespace observations */
} /* namespace niwa */
