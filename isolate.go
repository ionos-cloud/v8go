// Copyright 2019 Roger Chapman and the v8go contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package v8go

/*
#include <stdlib.h>
#include "v8go.h"
static RtnUnboundScript IsolateCompileUnboundScriptGo(IsolatePtr iso,
								_GoString_ src, _GoString_ org, CompileOptions options) {
	return IsolateCompileUnboundScript(iso, _GoStringPtr(src), _GoStringLen(src),
									_GoStringPtr(org), _GoStringLen(org), options); }
*/
import "C"

import (
	"runtime"
	"sync"
	"unsafe"
)

var v8once sync.Once

// Isolate is a JavaScript VM instance with its own heap and
// garbage collector. Most applications will create one isolate
// with many V8 contexts for execution.
type Isolate struct {
	ptr             C.IsolatePtr // V8 Isolate*
	internalContext *Context     // Default Context

	v8Mutex sync.Mutex       // Mutex for Lock() and Unlock() methods
	v8Lock  C.WithIsolatePtr // Holds native lock state between Lock() and Unlock()

	cbMutex sync.RWMutex             // Mutex for accessing `cbs`
	cbSeq   int                      // Latest ID assigned to a callback
	cbs     map[int]FunctionCallback // Array of registered callbacks

	stringBuffer []byte // Temporary scratch space for cgo to copy strings to

	null      *Value // Cached Value of `null`
	undefined *Value // Cached Value of `undefined`
	falseVal  *Value // Cached Value of `false`
	trueVal   *Value // Cached Value of `true`
}

// HeapStatistics represents V8 isolate heap statistics
type HeapStatistics struct {
	TotalHeapSize            uint64
	TotalHeapSizeExecutable  uint64
	TotalPhysicalSize        uint64
	TotalAvailableSize       uint64
	UsedHeapSize             uint64
	HeapSizeLimit            uint64
	MallocedMemory           uint64
	ExternalMemory           uint64
	PeakMallocedMemory       uint64
	NumberOfNativeContexts   uint64
	NumberOfDetachedContexts uint64
}

const kIsolateStringBufferSize = 1024

// NewIsolate creates a new V8 isolate. Only one thread may access
// a given isolate at a time, but different threads may access
// different isolates simultaneously.
// When an isolate is no longer used its resources should be freed
// by calling iso.Dispose().
// An *Isolate can be used as a v8go.ContextOption to create a new
// Context, rather than creating a new default Isolate.
func NewIsolate() *Isolate {
	return NewIsolateWith(0, 0)
}

// NewIsolateWith creates a new V8 isolate with control over the
// initial heap size and the maximum heap size. If the heap overflows
// the maximum size, the script will be terminated with an
// ExecutionTerminated exception.
// The heap sizes are given in bytes. If both are zero, the default
// heap settings are used.
func NewIsolateWith(initialHeap uint64, maxHeap uint64) *Isolate {
	v8once.Do(func() {
		C.Init()
	})
	result := C.NewIsolate(C.ulong(initialHeap), C.ulong(maxHeap))
	iso := &Isolate{
		ptr:          result.isolate,
		cbs:          make(map[int]FunctionCallback),
		stringBuffer: make([]byte, kIsolateStringBufferSize),
	}
	iso.internalContext = &Context{
		ptr: result.internalContext,
		iso: iso,
	}
	iso.null = &Value{result.nullVal, iso.internalContext}
	iso.undefined = &Value{result.undefinedVal, iso.internalContext}
	iso.falseVal = &Value{result.falseVal, iso.internalContext}
	iso.trueVal = &Value{result.trueVal, iso.internalContext}
	return iso
}

// TerminateExecution terminates forcefully the current thread
// of JavaScript execution in the given isolate.
func (i *Isolate) TerminateExecution() {
	C.IsolateTerminateExecution(i.ptr)
}

// IsExecutionTerminating returns whether V8 is currently terminating
// Javascript execution. If true, there are still JavaScript frames
// on the stack and the termination exception is still active.
func (i *Isolate) IsExecutionTerminating() bool {
	return C.IsolateIsExecutionTerminating(i.ptr) == 1
}

type CompileOptions struct {
	CachedData *CompilerCachedData

	Mode CompileMode
}

// CompileUnboundScript will create an UnboundScript (i.e. context-indepdent)
// using the provided source JavaScript, origin (a.k.a. filename), and options.
// If options contain a non-null CachedData, compilation of the script will use
// that code cache.
// error will be of type `JSError` if not nil.
func (i *Isolate) CompileUnboundScript(source, origin string, opts CompileOptions) (*UnboundScript, error) {
	var cOptions C.CompileOptions
	if opts.CachedData != nil {
		if opts.Mode != 0 {
			panic("On CompileOptions, Mode and CachedData can't both be set")
		}
		cOptions.compileOption = C.ScriptCompilerConsumeCodeCache
		cOptions.cachedData = C.ScriptCompilerCachedData{
			data:   (*C.uchar)(unsafe.Pointer(&opts.CachedData.Bytes[0])),
			length: C.int(len(opts.CachedData.Bytes)),
		}
	} else {
		cOptions.compileOption = C.int(opts.Mode)
	}

	rtn := C.IsolateCompileUnboundScriptGo(i.ptr, source, origin, cOptions)
	if rtn.ptr == nil {
		return nil, newJSError(rtn.error)
	}
	if opts.CachedData != nil {
		opts.CachedData.Rejected = int(rtn.cachedDataRejected) == 1
	}
	return &UnboundScript{
		ptr: rtn.ptr,
		iso: i,
	}, nil
}

// GetHeapStatistics returns heap statistics for an isolate.
func (i *Isolate) GetHeapStatistics() HeapStatistics {
	hs := C.IsolationGetHeapStatistics(i.ptr)

	return HeapStatistics{
		TotalHeapSize:            uint64(hs.total_heap_size),
		TotalHeapSizeExecutable:  uint64(hs.total_heap_size_executable),
		TotalPhysicalSize:        uint64(hs.total_physical_size),
		TotalAvailableSize:       uint64(hs.total_available_size),
		UsedHeapSize:             uint64(hs.used_heap_size),
		HeapSizeLimit:            uint64(hs.heap_size_limit),
		MallocedMemory:           uint64(hs.malloced_memory),
		ExternalMemory:           uint64(hs.external_memory),
		PeakMallocedMemory:       uint64(hs.peak_malloced_memory),
		NumberOfNativeContexts:   uint64(hs.number_of_native_contexts),
		NumberOfDetachedContexts: uint64(hs.number_of_detached_contexts),
	}
}

// Dispose will dispose the Isolate VM; subsequent calls will panic.
func (i *Isolate) Dispose() {
	if i.ptr == nil {
		return
	}
	if i.v8Lock != nil {
		i.Unlock()
	}
	C.IsolateDispose(i.ptr)
	i.ptr = nil
}

// Acquires a V8 lock on the Isolate for this thread. This speeds up subsequent calls involving
// Contexts, Values, Objects belonging to the Isolate.
// You MUST call Unlock when done. (Disposing the Isolate will call Unlock for you.)
// You MUST NOT make multiple calls to Lock; it's not recursive.
func (i *Isolate) Lock() {
	i.v8Mutex.Lock()
	if i.v8Lock != nil {
		panic("v8.Context.Lock called while already locked")
	}
	// LockOSThread ensures that C calls from this goroutine will always be made on the same
	// OS thread. This is absolutely necessary for making nested calls to v8::Locker (here and
	// then in whatever other methods are called) so that they'll be treated as nested calls and
	// not calls by different threads; otherwise the subsequent call will deadlock.
	runtime.LockOSThread()
	i.v8Lock = C.IsolateLock(i.ptr)
}

// Releases the V8 locks acquired by Lock.
func (i *Isolate) Unlock() {
	if i.v8Lock == nil {
		panic("v8.Context.Unlock called without first being locked")
	}
	C.IsolateUnlock(i.v8Lock)
	i.v8Lock = nil
	runtime.UnlockOSThread()
	i.v8Mutex.Unlock()
}

// ThrowException schedules an exception to be thrown when returning to
// JavaScript. When an exception has been scheduled it is illegal to invoke
// any JavaScript operation; the caller must return immediately and only after
// the exception has been handled does it become legal to invoke JavaScript operations.
func (i *Isolate) ThrowException(value *Value) *Value {
	if i.ptr == nil {
		panic("Isolate has been disposed")
	}
	return &Value{
		ref: C.IsolateThrowException(i.ptr, value.valuePtr()),
		ctx: value.ctx,
	}
}

// Deprecated: use `iso.Dispose()`.
func (i *Isolate) Close() {
	i.Dispose()
}

func (i *Isolate) apply(opts *contextOptions) {
	opts.iso = i
}

func (i *Isolate) registerCallback(cb FunctionCallback) int {
	i.cbMutex.Lock()
	i.cbSeq++
	ref := i.cbSeq
	i.cbs[ref] = cb
	i.cbMutex.Unlock()
	return ref
}

func (i *Isolate) getCallback(ref int) FunctionCallback {
	i.cbMutex.RLock()
	defer i.cbMutex.RUnlock()
	return i.cbs[ref]
}
