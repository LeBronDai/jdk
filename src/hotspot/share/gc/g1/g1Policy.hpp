/*
 * Copyright (c) 2016, 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_G1_G1POLICY_HPP
#define SHARE_GC_G1_G1POLICY_HPP

#include "gc/g1/g1CollectorState.hpp"
#include "gc/g1/g1GCPhaseTimes.hpp"
#include "gc/g1/g1HeapRegionAttr.hpp"
#include "gc/g1/g1InitialMarkToMixedTimeTracker.hpp"
#include "gc/g1/g1MMUTracker.hpp"
#include "gc/g1/g1RemSetTrackingPolicy.hpp"
#include "gc/g1/g1Predictions.hpp"
#include "gc/g1/g1YoungGenSizer.hpp"
#include "gc/shared/gcCause.hpp"
#include "utilities/pair.hpp"

// A G1Policy makes policy decisions that determine the
// characteristics of the collector.  Examples include:
//   * choice of collection set.
//   * when to collect.

class HeapRegion;
class G1CollectionSet;
class G1CollectionSetCandidates;
class G1CollectionSetChooser;
class G1IHOPControl;
class G1Analytics;
class G1SurvivorRegions;
class G1YoungGenSizer;
class GCPolicyCounters;
class STWGCTimer;

class G1Policy: public CHeapObj<mtGC> {
 private:

  static G1IHOPControl* create_ihop_control(const G1Predictions* predictor);
  // Update the IHOP control with necessary statistics.
  void update_ihop_prediction(double mutator_time_s,
                              size_t mutator_alloc_bytes,
                              size_t young_gen_size,
                              bool this_gc_was_young_only);
  void report_ihop_statistics();

  G1Predictions _predictor;
  G1Analytics* _analytics;
  G1RemSetTrackingPolicy _remset_tracker;
  G1MMUTracker* _mmu_tracker;
  G1IHOPControl* _ihop_control;

  GCPolicyCounters* _policy_counters;

  double _full_collection_start_sec;

  jlong _collection_pause_end_millis;

  uint _young_list_target_length;
  uint _young_list_fixed_length;

  // The max number of regions we can extend the eden by while the GC
  // locker is active. This should be >= _young_list_target_length;
  uint _young_list_max_length;

  // SurvRateGroups below must be initialized after the predictor because they
  // indirectly use it through this object passed to their constructor.
  SurvRateGroup* _short_lived_surv_rate_group;
  SurvRateGroup* _survivor_surv_rate_group;

  double _reserve_factor;
  // This will be set when the heap is expanded
  // for the first time during initialization.
  uint   _reserve_regions;

  G1YoungGenSizer* _young_gen_sizer;

  uint _free_regions_at_end_of_collection;

  size_t _rs_length;

  size_t _rs_length_prediction;

  size_t _pending_cards_at_gc_start;
  size_t _pending_cards_at_prev_gc_end;
  size_t _total_mutator_refined_cards;
  size_t _total_concurrent_refined_cards;
  Tickspan _total_concurrent_refinement_time;

  // The amount of allocated bytes in old gen during the last mutator and the following
  // young GC phase.
  size_t _bytes_allocated_in_old_since_last_gc;

  G1InitialMarkToMixedTimeTracker _initial_mark_to_mixed;

  bool should_update_surv_rate_group_predictors() {
    return collector_state()->in_young_only_phase() && !collector_state()->mark_or_rebuild_in_progress();
  }

  double logged_cards_processing_time() const;
public:
  const G1Predictions& predictor() const { return _predictor; }
  const G1Analytics* analytics()   const { return const_cast<const G1Analytics*>(_analytics); }

  G1RemSetTrackingPolicy* remset_tracker() { return &_remset_tracker; }

  // Add the given number of bytes to the total number of allocated bytes in the old gen.
  void add_bytes_allocated_in_old_since_last_gc(size_t bytes) { _bytes_allocated_in_old_since_last_gc += bytes; }

  void set_region_eden(HeapRegion* hr) {
    hr->set_eden();
    hr->install_surv_rate_group(_short_lived_surv_rate_group);
  }

  void set_region_survivor(HeapRegion* hr) {
    assert(hr->is_survivor(), "pre-condition");
    hr->install_surv_rate_group(_survivor_surv_rate_group);
  }

  void record_rs_length(size_t rs_length) {
    _rs_length = rs_length;
  }

  double predict_base_elapsed_time_ms(size_t num_pending_cards) const;
  double predict_base_elapsed_time_ms(size_t num_pending_cards,
                                      size_t rs_length) const;
  size_t predict_bytes_to_copy(HeapRegion* hr) const;
  double predict_region_elapsed_time_ms(HeapRegion* hr, bool for_young_gc) const;

  double predict_survivor_regions_evac_time() const;

  void cset_regions_freed() {
    bool update = should_update_surv_rate_group_predictors();

    _short_lived_surv_rate_group->all_surviving_words_recorded(predictor(), update);
    _survivor_surv_rate_group->all_surviving_words_recorded(predictor(), update);
  }

  G1MMUTracker* mmu_tracker() {
    return _mmu_tracker;
  }

  const G1MMUTracker* mmu_tracker() const {
    return _mmu_tracker;
  }

  double max_pause_time_ms() const {
    return _mmu_tracker->max_gc_time() * 1000.0;
  }

  double predict_yg_surv_rate(int age, SurvRateGroup* surv_rate_group) const;

  double predict_yg_surv_rate(int age) const;

  double accum_yg_surv_rate_pred(int age) const;

private:
  G1CollectionSet* _collection_set;
  double average_time_ms(G1GCPhaseTimes::GCParPhases phase) const;
  double other_time_ms(double pause_time_ms) const;

  double young_other_time_ms() const;
  double non_young_other_time_ms() const;
  double constant_other_time_ms(double pause_time_ms) const;

  G1CollectionSetChooser* cset_chooser() const;

  // Stash a pointer to the g1 heap.
  G1CollectedHeap* _g1h;

  G1GCPhaseTimes* _phase_times;

  // This set of variables tracks the collector efficiency, in order to
  // determine whether we should initiate a new marking.
  double _mark_remark_start_sec;
  double _mark_cleanup_start_sec;

  // Updates the internal young list maximum and target lengths. Returns the
  // unbounded young list target length. If no rs_length parameter is passed,
  // predict the RS length using the prediction model, otherwise use the
  // given rs_length as the prediction.
  uint update_young_list_max_and_target_length();
  uint update_young_list_max_and_target_length(size_t rs_length);

  // Update the young list target length either by setting it to the
  // desired fixed value or by calculating it using G1's pause
  // prediction model.
  // Returns the unbounded young list target length.
  uint update_young_list_target_length(size_t rs_length);

  // Calculate and return the minimum desired young list target
  // length. This is the minimum desired young list length according
  // to the user's inputs.
  uint calculate_young_list_desired_min_length(uint base_min_length) const;

  // Calculate and return the maximum desired young list target
  // length. This is the maximum desired young list length according
  // to the user's inputs.
  uint calculate_young_list_desired_max_length() const;

  // Calculate and return the maximum young list target length that
  // can fit into the pause time goal. The parameters are: rs_length
  // represent the prediction of how large the young RSet lengths will
  // be, base_min_length is the already existing number of regions in
  // the young list, min_length and max_length are the desired min and
  // max young list length according to the user's inputs.
  uint calculate_young_list_target_length(size_t rs_length,
                                          uint base_min_length,
                                          uint desired_min_length,
                                          uint desired_max_length) const;

  // Result of the bounded_young_list_target_length() method, containing both the
  // bounded as well as the unbounded young list target lengths in this order.
  typedef Pair<uint, uint, StackObj> YoungTargetLengths;
  YoungTargetLengths young_list_target_lengths(size_t rs_length) const;

  void update_rs_length_prediction();
  void update_rs_length_prediction(size_t prediction);

  // Check whether a given young length (young_length) fits into the
  // given target pause time and whether the prediction for the amount
  // of objects to be copied for the given length will fit into the
  // given free space (expressed by base_free_regions).  It is used by
  // calculate_young_list_target_length().
  bool predict_will_fit(uint young_length, double base_time_ms,
                        uint base_free_regions, double target_pause_time_ms) const;

public:
  size_t pending_cards_at_gc_start() const { return _pending_cards_at_gc_start; }

  // Calculate the minimum number of old regions we'll add to the CSet
  // during a mixed GC.
  uint calc_min_old_cset_length() const;

  // Calculate the maximum number of old regions we'll add to the CSet
  // during a mixed GC.
  uint calc_max_old_cset_length() const;

  // Returns the given amount of reclaimable bytes (that represents
  // the amount of reclaimable space still to be collected) as a
  // percentage of the current heap capacity.
  double reclaimable_bytes_percent(size_t reclaimable_bytes) const;

  jlong collection_pause_end_millis() { return _collection_pause_end_millis; }

private:
  void clear_collection_set_candidates();
  // Sets up marking if proper conditions are met.
  void maybe_start_marking();

  // The kind of STW pause.
  enum PauseKind {
    FullGC,
    YoungOnlyGC,
    MixedGC,
    LastYoungGC,
    InitialMarkGC,
    Cleanup,
    Remark
  };

  // Calculate PauseKind from internal state.
  PauseKind young_gc_pause_kind() const;
  // Record the given STW pause with the given start and end times (in s).
  void record_pause(PauseKind kind, double start, double end);
  // Indicate that we aborted marking before doing any mixed GCs.
  void abort_time_to_mixed_tracking();

  void record_concurrent_refinement_data(bool is_full_collection);

public:

  G1Policy(STWGCTimer* gc_timer);

  virtual ~G1Policy();

  static G1Policy* create_policy(STWGCTimer* gc_timer_stw);

  G1CollectorState* collector_state() const;

  G1GCPhaseTimes* phase_times() const { return _phase_times; }

  // Check the current value of the young list RSet length and
  // compare it against the last prediction. If the current value is
  // higher, recalculate the young list target length prediction.
  void revise_young_list_target_length_if_necessary(size_t rs_length);

  // This should be called after the heap is resized.
  void record_new_heap_size(uint new_number_of_regions);

  virtual void init(G1CollectedHeap* g1h, G1CollectionSet* collection_set);

  void note_gc_start();

  bool need_to_start_conc_mark(const char* source, size_t alloc_word_size = 0);

  bool about_to_start_mixed_phase() const;

  // Record the start and end of an evacuation pause.
  void record_collection_pause_start(double start_time_sec);
  virtual void record_collection_pause_end(double pause_time_ms);

  // Record the start and end of a full collection.
  void record_full_collection_start();
  virtual void record_full_collection_end();

  // Must currently be called while the world is stopped.
  void record_concurrent_mark_init_end(double mark_init_elapsed_time_ms);

  // Record start and end of remark.
  void record_concurrent_mark_remark_start();
  void record_concurrent_mark_remark_end();

  // Record start, end, and completion of cleanup.
  void record_concurrent_mark_cleanup_start();
  void record_concurrent_mark_cleanup_end();

  void print_phases();

  bool next_gc_should_be_mixed(const char* true_action_str,
                               const char* false_action_str) const;

  // Calculate and return the number of initial and optional old gen regions from
  // the given collection set candidates and the remaining time.
  void calculate_old_collection_set_regions(G1CollectionSetCandidates* candidates,
                                            double time_remaining_ms,
                                            uint& num_initial_regions,
                                            uint& num_optional_regions);

  // Calculate the number of optional regions from the given collection set candidates,
  // the remaining time and the maximum number of these regions and return the number
  // of actually selected regions in num_optional_regions.
  void calculate_optional_collection_set_regions(G1CollectionSetCandidates* candidates,
                                                 uint const max_optional_regions,
                                                 double time_remaining_ms,
                                                 uint& num_optional_regions);

private:
  // Set the state to start a concurrent marking cycle and clear
  // _initiate_conc_mark_if_possible because it has now been
  // acted on.
  void initiate_conc_mark();

public:
  // This sets the initiate_conc_mark_if_possible() flag to start a
  // new cycle, as long as we are not already in one. It's best if it
  // is called during a safepoint when the test whether a cycle is in
  // progress or not is stable.
  bool force_initial_mark_if_outside_cycle(GCCause::Cause gc_cause);

  // This is called at the very beginning of an evacuation pause (it
  // has to be the first thing that the pause does). If
  // initiate_conc_mark_if_possible() is true, and the concurrent
  // marking thread has completed its work during the previous cycle,
  // it will set in_initial_mark_gc() to so that the pause does
  // the initial-mark work and start a marking cycle.
  void decide_on_conc_mark_initiation();

  void finished_recalculating_age_indexes(bool is_survivors) {
    if (is_survivors) {
      _survivor_surv_rate_group->finished_recalculating_age_indexes();
    } else {
      _short_lived_surv_rate_group->finished_recalculating_age_indexes();
    }
  }

  size_t young_list_target_length() const { return _young_list_target_length; }

  bool should_allocate_mutator_region() const;

  bool can_expand_young_list() const;

  uint young_list_max_length() const {
    return _young_list_max_length;
  }

  bool use_adaptive_young_list_length() const;

  void transfer_survivors_to_cset(const G1SurvivorRegions* survivors);

private:
  //
  // Survivor regions policy.
  //

  // Current tenuring threshold, set to 0 if the collector reaches the
  // maximum amount of survivors regions.
  uint _tenuring_threshold;

  // The limit on the number of regions allocated for survivors.
  uint _max_survivor_regions;

  AgeTable _survivors_age_table;

  size_t desired_survivor_size(uint max_regions) const;

  // Fraction used when predicting how many optional regions to include in
  // the CSet. This fraction of the available time is used for optional regions,
  // the rest is used to add old regions to the normal CSet.
  double optional_prediction_fraction() { return 0.2; }

public:
  // Fraction used when evacuating the optional regions. This fraction of the
  // remaining time is used to choose what regions to include in the evacuation.
  double optional_evacuation_fraction() { return 0.75; }

  uint tenuring_threshold() const { return _tenuring_threshold; }

  uint max_survivor_regions() {
    return _max_survivor_regions;
  }

  void note_start_adding_survivor_regions() {
    _survivor_surv_rate_group->start_adding_regions();
  }

  void note_stop_adding_survivor_regions() {
    _survivor_surv_rate_group->stop_adding_regions();
  }

  void record_age_table(AgeTable* age_table) {
    _survivors_age_table.merge(age_table);
  }

  void print_age_table();

  void update_max_gc_locker_expansion();

  void update_survivors_policy();

  virtual bool force_upgrade_to_full() {
    return false;
  }
};

#endif // SHARE_GC_G1_G1POLICY_HPP
