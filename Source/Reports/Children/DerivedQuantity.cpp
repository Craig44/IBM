/*
 * DerivedQuantity.cpp
 *
 *  Created on: 4/09/2013
 *      Author: Admin
 */

#include "DerivedQuantity.h"

#include "DerivedQuantities/Manager.h"

namespace niwa {
namespace reports {


/**
 *
 */
DerivedQuantity::DerivedQuantity(Model* model) : Report(model) {
  run_mode_    = (RunMode::Type)(RunMode::kBasic);
  model_state_ = (State::Type)(State::kIterationComplete);
}


/**
 *
 */
void DerivedQuantity::DoExecute() {
  LOG_TRACE();
  derivedquantities::Manager& manager = *model_->managers().derived_quantity();
  cache_ << "*"<< type_ << "[" << label_ << "]" << "\n";

  auto derived_quantities = manager.objects();
  for (auto dq : derived_quantities) {
    string label =  dq->label();
    cache_ << label << " " << REPORT_R_LIST <<" \n";
    cache_ << "type: " << dq->type() << " \n";
    vector<vector<float>> init_values = dq->initialisation_values();
    for (unsigned i = 0; i < init_values.size(); ++i) {
      cache_ << "initialisation_phase["<< i + 1 << "]: ";
      cache_ << init_values[i].back() << " ";
      cache_ << "\n";
    }


    const map<unsigned, float> values = dq->values();
    cache_ << "values " << REPORT_R_VECTOR <<"\n";
    for (auto iter = values.begin(); iter != values.end(); ++iter) {
        float weight = iter->second;
        cache_ << iter->first << " " << weight << "\n";
    }
    //cache_ <<"\n";
    cache_ << REPORT_R_LIST_END <<"\n";

  }
  ready_for_writing_ = true;
}

} /* namespace reports */
} /* namespace niwa */