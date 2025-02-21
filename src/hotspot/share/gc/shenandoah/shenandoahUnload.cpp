/*
 * Copyright (c) 2019, Red Hat, Inc. All rights reserved.
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

#include "precompiled.hpp"

#include "classfile/classLoaderDataGraph.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeBehaviours.hpp"
#include "code/codeCache.hpp"
#include "code/dependencyContext.hpp"
#include "gc/shared/gcBehaviours.hpp"
#include "gc/shared/suspendibleThreadSet.hpp"
#include "gc/shenandoah/shenandoahClosures.inline.hpp"
#include "gc/shenandoah/shenandoahCodeRoots.hpp"
#include "gc/shenandoah/shenandoahConcurrentRoots.hpp"
#include "gc/shenandoah/shenandoahNMethod.inline.hpp"
#include "gc/shenandoah/shenandoahLock.hpp"
#include "gc/shenandoah/shenandoahRootProcessor.hpp"
#include "gc/shenandoah/shenandoahUnload.hpp"
#include "gc/shenandoah/shenandoahVerifier.hpp"
#include "memory/iterator.hpp"
#include "memory/resourceArea.hpp"
#include "oops/access.inline.hpp"

class ShenandoahIsUnloadingOopClosure : public OopClosure {
private:
  ShenandoahMarkingContext*    _marking_context;
  bool                         _is_unloading;

public:
  ShenandoahIsUnloadingOopClosure() :
    _marking_context(ShenandoahHeap::heap()->marking_context()),
    _is_unloading(false) {
  }

  virtual void do_oop(oop* p) {
    if (_is_unloading) {
      return;
    }

    const oop o = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(o) &&
        _marking_context->is_complete() &&
        !_marking_context->is_marked(o)) {
      _is_unloading = true;
    }
  }

  virtual void do_oop(narrowOop* p) {
    ShouldNotReachHere();
  }

  bool is_unloading() const {
    return _is_unloading;
  }
};

class ShenandoahIsUnloadingBehaviour : public IsUnloadingBehaviour {
public:
  virtual bool is_unloading(CompiledMethod* method) const {
    nmethod* const nm = method->as_nmethod();
    guarantee(ShenandoahHeap::heap()->is_evacuation_in_progress(), "Only this phase");
    ShenandoahNMethod* data = ShenandoahNMethod::gc_data(nm);
    ShenandoahReentrantLocker locker(data->lock());
    ShenandoahIsUnloadingOopClosure cl;
    data->oops_do(&cl);
    return  cl.is_unloading();
  }
};

class ShenandoahCompiledICProtectionBehaviour : public CompiledICProtectionBehaviour {
public:
  virtual bool lock(CompiledMethod* method) {
    nmethod* const nm = method->as_nmethod();
    ShenandoahReentrantLock* const lock = ShenandoahNMethod::lock_for_nmethod(nm);
    assert(lock != NULL, "Not yet registered?");
    lock->lock();
    return true;
  }

  virtual void unlock(CompiledMethod* method) {
    nmethod* const nm = method->as_nmethod();
    ShenandoahReentrantLock* const lock = ShenandoahNMethod::lock_for_nmethod(nm);
    assert(lock != NULL, "Not yet registered?");
    lock->unlock();
  }

  virtual bool is_safe(CompiledMethod* method) {
    if (SafepointSynchronize::is_at_safepoint()) {
      return true;
    }

    nmethod* const nm = method->as_nmethod();
    ShenandoahReentrantLock* const lock = ShenandoahNMethod::lock_for_nmethod(nm);
    assert(lock != NULL, "Not yet registered?");
    return lock->owned_by_self();
  }
};

ShenandoahUnload::ShenandoahUnload() {
  if (ShenandoahConcurrentRoots::can_do_concurrent_class_unloading()) {
    static ShenandoahIsUnloadingBehaviour is_unloading_behaviour;
    IsUnloadingBehaviour::set_current(&is_unloading_behaviour);

    static ShenandoahCompiledICProtectionBehaviour ic_protection_behaviour;
    CompiledICProtectionBehaviour::set_current(&ic_protection_behaviour);
  }
}

void ShenandoahUnload::prepare() {
  assert(SafepointSynchronize::is_at_safepoint(), "Should be at safepoint");
  assert(ShenandoahConcurrentRoots::can_do_concurrent_class_unloading(), "Sanity");
  CodeCache::increment_unloading_cycle();
  DependencyContext::cleaning_start();
}

void ShenandoahUnload::unlink() {
  SuspendibleThreadSetJoiner sts;
  bool unloading_occurred;
  ShenandoahHeap* const heap = ShenandoahHeap::heap();
  {
    MutexLocker cldg_ml(ClassLoaderDataGraph_lock);
    unloading_occurred = SystemDictionary::do_unloading(heap->gc_timer());
  }

  Klass::clean_weak_klass_links(unloading_occurred);
  ShenandoahCodeRoots::unlink(ShenandoahHeap::heap()->workers(), unloading_occurred);
  DependencyContext::cleaning_end();
}

void ShenandoahUnload::purge() {
  {
    SuspendibleThreadSetJoiner sts;
    ShenandoahCodeRoots::purge(ShenandoahHeap::heap()->workers());
  }

  ClassLoaderDataGraph::purge();
  CodeCache::purge_exception_caches();
}

class ShenandoahUnloadRendezvousClosure : public ThreadClosure {
public:
  void do_thread(Thread* thread) {}
};

void ShenandoahUnload::unload() {
  assert(ShenandoahConcurrentRoots::can_do_concurrent_class_unloading(), "Why we here?");
  if (!ShenandoahHeap::heap()->is_evacuation_in_progress()) {
    return;
  }

  // Unlink stale metadata and nmethods
  unlink();

  // Make sure stale metadata and nmethods are no longer observable
  ShenandoahUnloadRendezvousClosure cl;
  Handshake::execute(&cl);

  // Purge stale metadata and nmethods that were unlinked
  purge();
}

void ShenandoahUnload::finish() {
  MetaspaceGC::compute_new_size();
  MetaspaceUtils::verify_metrics();
}
