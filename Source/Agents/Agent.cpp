/**
 * @file Agent.cpp
 * @author  C.Marsh
 * @version 1.0
 * @date 11/06/2018
 * @section LICENSE
 *
 * Copyright NIWA Science �2012 - www.niwa.co.nz
 *
 */

// Headers
#include "Agent.h"

#include "Model/Model.h"
#include "Utilities/RandomNumberGenerator.h"

// Namespaces
namespace niwa {


Agent::Agent(float lat, float lon, float first_age_length_par, float second_age_length_par, float third_age_length_par, float M, unsigned birth_year,
    float first_length_weight_par, float second_length_weigth_par, Model* model, bool mature, unsigned sex, float scalar,
    unsigned home_row, unsigned home_col, unsigned tag) :
    lat_(lat),
    lon_(lon),
    first_age_length_par_(first_age_length_par),
    second_age_length_par_(second_age_length_par),
    third_age_length_par_(third_age_length_par),
    M_(M),
    birth_year_(birth_year),
    first_length_weight_par_(first_length_weight_par),
    second_length_weight_par_(second_length_weigth_par),
    model_(model),
    mature_(mature),
    sex_(sex),
    scalar_(scalar),
    home_row_(home_row),
    home_col_(home_col),
    tag_(tag)

{
  age_ = std::min((model_->current_year() - birth_year_), model_->max_age());
  growth_init(); // if age = 0 will calculate length_ and weight_ based on uniform(0.000,0.4)
}


Agent::Agent(float lat, float lon, float first_age_length_par, float second_age_length_par, float third_age_length_par, float fourth_age_length_par, float fifth_age_length_par, float sixth_age_length_par,
    float M, unsigned birth_year, float first_length_weight_par, float second_length_weigth_par,
    Model* model, bool mature, unsigned sex, float scalar, unsigned home_row, unsigned home_col, unsigned tag) :
    lat_(lat),
    lon_(lon),
    first_age_length_par_(first_age_length_par),
    second_age_length_par_(second_age_length_par),
    third_age_length_par_(third_age_length_par),
    fourth_age_length_par_(fourth_age_length_par),
    fith_age_length_par_(fifth_age_length_par),
    sixth_age_length_par_(sixth_age_length_par),
    M_(M),
    birth_year_(birth_year),
    first_length_weight_par_(first_length_weight_par),
    second_length_weight_par_(second_length_weigth_par),
    model_(model),
    mature_(mature),
    sex_(sex),
    scalar_(scalar),
    home_row_(home_row),
    home_col_(home_col),
    tag_(tag)

{
  age_ = std::min((model_->current_year() - birth_year_), model_->max_age());
  growth_init(); // if age = 0 will calculate length_ and weight_
}

/*// An overloaded constructor for cloning an agent
Agent::Agent(Agent& agent_to_copy) {
  lat_ = agent_to_copy.get_lat();
  lon_ = agent_to_copy.get_lon();
  first_age_length_par_ = agent_to_copy.first_age_length_par();
  second_age_length_par_ = agent_to_copy.first_age_length_par();

}*/
// Return Age this allows for implicit ageing, which is handy as it reduces a process out of the dynamics
void Agent::birthday() {
    //unsigned age =  std::min((model_->current_year() - birth_year_), model_->max_age());
    age_ = std::min(age_ + 1, model_->max_age());
}
// Return Age this allows for implicit ageing, which is handy as it reduces a process out of the dynamics
unsigned Agent::get_age() {
    //unsigned age =  std::min((model_->current_year() - birth_year_), model_->max_age());
    return age_;
}

// an index for which length bin the individual falls in.
unsigned Agent::get_length_bin_index() {
  vector<unsigned> lengths = model_->length_bins();
  for(unsigned length_max = 1; length_max < lengths.size(); ++length_max) {
    if (length_ <= lengths[length_max])
      return length_max - 1;
  }
  return lengths.size() - 2;
}


// an index for which length the individual falls in.
unsigned Agent::get_age_index() {
  return get_age() - model_->min_age();
}


void Agent::apply_tagging_event(unsigned tags, unsigned row, unsigned col) {
  length_at_tag_ = length_;
  tag_ = tags;
  tag_time_step_ = model_->get_time_step_counter();
  tag_row_ = row;
  tag_col_ = col;
  tag_year_ = model_->current_year();
}
/*
 * An internal function to set initial length at age when initially seeding agents in the world,
 * So that we have the equivalent length and weight frequency. Calculate expected length at age assuming von Bert parameters
 *
*/
void Agent::growth_init() {
  //LOG_FINE() << "here";
  //utilities::RandomNumberGenerator& rng = utilities::RandomNumberGenerator::Instance();
  if (model_->get_growth_model() == Growth::kVonbert) {
    length_ = first_age_length_par_ * (1 - std::exp(-second_age_length_par_ * ((float)get_age() - third_age_length_par_)));

  } else if(model_->get_growth_model() == Growth::kSchnute)  {
    length_ = pow(pow(fourth_age_length_par_, second_age_length_par_) + (pow(fith_age_length_par_, second_age_length_par_) - pow(fourth_age_length_par_, second_age_length_par_)) * ((1.0 - std::exp(-first_age_length_par_ * ((float)get_age() -  (float)model_->min_age()))) / (1.0 - std::exp(-first_age_length_par_ *(sixth_age_length_par_ - (float)model_->min_age())))), 1 / second_age_length_par_);
  }
  
  if(length_ <= 0) 
    length_ = 0.0;

  weight_ = first_length_weight_par_ * pow(length_, second_length_weight_par_); // Just update weight when ever we update length to save executions
  //LOG_MEDIUM() << "length = " << length_ << " weight = " << weight_ << " age = " << get_age();
}

} /* namespace niwa */
