/*
 * Copyright (c) 2017, 2019, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_GC_SHENANDOAH_SHENANDOAHCODEROOTS_HPP
#define SHARE_GC_SHENANDOAH_SHENANDOAHCODEROOTS_HPP

#include "code/codeCache.hpp"
#include "gc/shenandoah/shenandoahSharedVariables.hpp"
#include "gc/shenandoah/shenandoahLock.hpp"
#include "gc/shenandoah/shenandoahNMethod.hpp"
#include "memory/allocation.hpp"
#include "memory/iterator.hpp"

class ShenandoahHeap;
class ShenandoahHeapRegion;

class ShenandoahParallelCodeHeapIterator {
  friend class CodeCache;
private:
  CodeHeap*     _heap;
  DEFINE_PAD_MINUS_SIZE(0, DEFAULT_CACHE_LINE_SIZE, sizeof(volatile int));
  volatile int  _claimed_idx;
  volatile bool _finished;
  DEFINE_PAD_MINUS_SIZE(1, DEFAULT_CACHE_LINE_SIZE, 0);
public:
  ShenandoahParallelCodeHeapIterator(CodeHeap* heap);
  void parallel_blobs_do(CodeBlobClosure* f);
};

class ShenandoahParallelCodeCacheIterator {
  friend class CodeCache;
private:
  ShenandoahParallelCodeHeapIterator* _iters;
  int                       _length;

private:
  // Noncopyable.
  ShenandoahParallelCodeCacheIterator(const ShenandoahParallelCodeCacheIterator& o);
  ShenandoahParallelCodeCacheIterator& operator=(const ShenandoahParallelCodeCacheIterator& o);
public:
  ShenandoahParallelCodeCacheIterator(const GrowableArray<CodeHeap*>* heaps);
  ~ShenandoahParallelCodeCacheIterator();
  void parallel_blobs_do(CodeBlobClosure* f);
};

class ShenandoahCodeRootsIterator {
  friend class ShenandoahCodeRoots;
protected:
  ShenandoahParallelCodeCacheIterator _par_iterator;
  ShenandoahSharedFlag _seq_claimed;
  ShenandoahNMethodTableSnapshot* _table_snapshot;

protected:
  ShenandoahCodeRootsIterator();
  ~ShenandoahCodeRootsIterator();

  template<bool CSET_FILTER>
  void dispatch_parallel_blobs_do(CodeBlobClosure *f);

  template<bool CSET_FILTER>
  void fast_parallel_blobs_do(CodeBlobClosure *f);
};

class ShenandoahAllCodeRootsIterator : public ShenandoahCodeRootsIterator {
public:
  ShenandoahAllCodeRootsIterator() : ShenandoahCodeRootsIterator() {};
  void possibly_parallel_blobs_do(CodeBlobClosure *f);
};

class ShenandoahCsetCodeRootsIterator : public ShenandoahCodeRootsIterator {
public:
  ShenandoahCsetCodeRootsIterator() : ShenandoahCodeRootsIterator() {};
  void possibly_parallel_blobs_do(CodeBlobClosure* f);
};

class ShenandoahCodeRoots : public AllStatic {
  friend class ShenandoahHeap;
  friend class ShenandoahCodeRootsIterator;

public:
  static void initialize();
  static void register_nmethod(nmethod* nm);
  static void unregister_nmethod(nmethod* nm);
  static void flush_nmethod(nmethod* nm);

  static ShenandoahNMethodTable* table() {
    return _nmethod_table;
  }

  // Concurrent nmethod unloading support
  static void unlink(WorkGang* workers, bool unloading_occurred);
  static void purge(WorkGang* workers);
  static void prepare_concurrent_unloading();
  static int  disarmed_value()         { return _disarmed_value; }
  static int* disarmed_value_address() { return &_disarmed_value; }

private:
  static ShenandoahNMethodTable* _nmethod_table;
  static int                     _disarmed_value;
};

#endif // SHARE_GC_SHENANDOAH_SHENANDOAHCODEROOTS_HPP
