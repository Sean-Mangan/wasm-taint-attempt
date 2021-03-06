// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/assembler.h"
#include "src/compiler/wasm-compiler.h"
#include "src/conversions.h"
#include "src/debug/debug.h"
#include "src/factory.h"
#include "src/frame-constants.h"
#include "src/objects-inl.h"
#include "src/objects/frame-array-inl.h"
#include "src/trap-handler/trap-handler.h"
#include "src/v8memory.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-objects.h"

//ADDED HERE
#include "src/frames-inl.h"

#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#define STRING(s) #s
//END ADD HERE


namespace v8 {
namespace internal {

namespace {

WasmInstanceObject* GetWasmInstanceOnStackTop(Isolate* isolate) {
  DisallowHeapAllocation no_allocation;
  const Address entry = Isolate::c_entry_fp(isolate->thread_local_top());
  Address pc =
      Memory::Address_at(entry + StandardFrameConstants::kCallerPCOffset);
  WasmInstanceObject* owning_instance = nullptr;
  if (FLAG_wasm_jit_to_native) {
    owning_instance = WasmInstanceObject::GetOwningInstance(
        isolate->wasm_engine()->code_manager()->LookupCode(pc));
  } else {
    owning_instance = WasmInstanceObject::GetOwningInstanceGC(
        isolate->inner_pointer_to_code_cache()->GetCacheEntry(pc)->code);
  }
  CHECK_NOT_NULL(owning_instance);
  return owning_instance;
}

Context* GetWasmContextOnStackTop(Isolate* isolate) {
  return GetWasmInstanceOnStackTop(isolate)
      ->compiled_module()
      ->native_context();
}

class ClearThreadInWasmScope {
 public:
  explicit ClearThreadInWasmScope(bool coming_from_wasm)
      : coming_from_wasm_(coming_from_wasm) {
    DCHECK_EQ(trap_handler::IsTrapHandlerEnabled() && coming_from_wasm,
              trap_handler::IsThreadInWasm());
    if (coming_from_wasm) trap_handler::ClearThreadInWasm();
  }
  ~ClearThreadInWasmScope() {
    DCHECK(!trap_handler::IsThreadInWasm());
    if (coming_from_wasm_) trap_handler::SetThreadInWasm();
  }

 private:
  const bool coming_from_wasm_;
};

}  // namespace

RUNTIME_FUNCTION(Runtime_WasmGrowMemory) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_UINT32_ARG_CHECKED(delta_pages, 0);
  Handle<WasmInstanceObject> instance(GetWasmInstanceOnStackTop(isolate),
                                      isolate);

  // This runtime function is always being called from wasm code.
  ClearThreadInWasmScope flag_scope(true);

  // Set the current isolate's context.
  DCHECK_NULL(isolate->context());
  isolate->set_context(instance->compiled_module()->native_context());

  return *isolate->factory()->NewNumberFromInt(
      WasmInstanceObject::GrowMemory(isolate, instance, delta_pages));
}

RUNTIME_FUNCTION(Runtime_ThrowWasmError) {
  DCHECK_EQ(1, args.length());
  CONVERT_SMI_ARG_CHECKED(message_id, 0);
  ClearThreadInWasmScope clear_wasm_flag(isolate->context() == nullptr);

  HandleScope scope(isolate);
  DCHECK_NULL(isolate->context());
  isolate->set_context(GetWasmContextOnStackTop(isolate));
  Handle<Object> error_obj = isolate->factory()->NewWasmRuntimeError(
      static_cast<MessageTemplate::Template>(message_id));
  return isolate->Throw(*error_obj);
}

RUNTIME_FUNCTION(Runtime_ThrowWasmStackOverflow) {
  SealHandleScope shs(isolate);
  DCHECK_LE(0, args.length());
  DCHECK_NULL(isolate->context());
  isolate->set_context(GetWasmContextOnStackTop(isolate));
  return isolate->StackOverflow();
}

RUNTIME_FUNCTION(Runtime_WasmThrowTypeError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kWasmTrapTypeError));
}

RUNTIME_FUNCTION(Runtime_WasmThrowCreate) {
  // TODO(kschimpf): Can this be replaced with equivalent TurboFan code/calls.
  HandleScope scope(isolate);
  DCHECK_NULL(isolate->context());
  isolate->set_context(GetWasmContextOnStackTop(isolate));
  DCHECK_EQ(2, args.length());
  Handle<Object> exception = isolate->factory()->NewWasmRuntimeError(
      static_cast<MessageTemplate::Template>(
          MessageTemplate::kWasmExceptionError));
  isolate->set_wasm_caught_exception(*exception);
  CONVERT_ARG_HANDLE_CHECKED(Smi, id, 0);
  CHECK(!JSReceiver::SetProperty(exception,
                                 isolate->factory()->InternalizeUtf8String(
                                     wasm::WasmException::kRuntimeIdStr),
                                 id, LanguageMode::kStrict)
             .is_null());
  CONVERT_SMI_ARG_CHECKED(size, 1);
  Handle<JSTypedArray> values =
      isolate->factory()->NewJSTypedArray(ElementsKind::UINT16_ELEMENTS, size);
  CHECK(!JSReceiver::SetProperty(exception,
                                 isolate->factory()->InternalizeUtf8String(
                                     wasm::WasmException::kRuntimeValuesStr),
                                 values, LanguageMode::kStrict)
             .is_null());
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmThrow) {
  // TODO(kschimpf): Can this be replaced with equivalent TurboFan code/calls.
  HandleScope scope(isolate);
  DCHECK_NULL(isolate->context());
  isolate->set_context(GetWasmContextOnStackTop(isolate));
  DCHECK_EQ(0, args.length());
  Handle<Object> exception(isolate->get_wasm_caught_exception(), isolate);
  CHECK(!exception.is_null());
  isolate->clear_wasm_caught_exception();
  return isolate->Throw(*exception);
}

RUNTIME_FUNCTION(Runtime_WasmGetExceptionRuntimeId) {
  // TODO(kschimpf): Can this be replaced with equivalent TurboFan code/calls.
  HandleScope scope(isolate);
  DCHECK_NULL(isolate->context());
  isolate->set_context(GetWasmContextOnStackTop(isolate));
  Handle<Object> except_obj(isolate->get_wasm_caught_exception(), isolate);
  if (!except_obj.is_null() && except_obj->IsJSReceiver()) {
    Handle<JSReceiver> exception(JSReceiver::cast(*except_obj));
    Handle<Object> tag;
    if (JSReceiver::GetProperty(exception,
                                isolate->factory()->InternalizeUtf8String(
                                    wasm::WasmException::kRuntimeIdStr))
            .ToHandle(&tag)) {
      if (tag->IsSmi()) {
        return *tag;
      }
    }
  }
  return Smi::FromInt(wasm::kInvalidExceptionTag);
}

RUNTIME_FUNCTION(Runtime_WasmExceptionGetElement) {
  // TODO(kschimpf): Can this be replaced with equivalent TurboFan code/calls.
  HandleScope scope(isolate);
  DCHECK_NULL(isolate->context());
  isolate->set_context(GetWasmContextOnStackTop(isolate));
  DCHECK_EQ(1, args.length());
  Handle<Object> except_obj(isolate->get_wasm_caught_exception(), isolate);
  if (!except_obj.is_null() && except_obj->IsJSReceiver()) {
    Handle<JSReceiver> exception(JSReceiver::cast(*except_obj));
    Handle<Object> values_obj;
    if (JSReceiver::GetProperty(exception,
                                isolate->factory()->InternalizeUtf8String(
                                    wasm::WasmException::kRuntimeValuesStr))
            .ToHandle(&values_obj)) {
      if (values_obj->IsJSTypedArray()) {
        Handle<JSTypedArray> values = Handle<JSTypedArray>::cast(values_obj);
        CHECK_EQ(values->type(), kExternalUint16Array);
        CONVERT_SMI_ARG_CHECKED(index, 0);
        CHECK_LT(index, Smi::ToInt(values->length()));
        auto* vals =
            reinterpret_cast<uint16_t*>(values->GetBuffer()->allocation_base());
        return Smi::FromInt(vals[index]);
      }
    }
  }
  return Smi::FromInt(0);
}

RUNTIME_FUNCTION(Runtime_WasmExceptionSetElement) {
  // TODO(kschimpf): Can this be replaced with equivalent TurboFan code/calls.
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DCHECK_NULL(isolate->context());
  isolate->set_context(GetWasmContextOnStackTop(isolate));
  Handle<Object> except_obj(isolate->get_wasm_caught_exception(), isolate);
  if (!except_obj.is_null() && except_obj->IsJSReceiver()) {
    Handle<JSReceiver> exception(JSReceiver::cast(*except_obj));
    Handle<Object> values_obj;
    if (JSReceiver::GetProperty(exception,
                                isolate->factory()->InternalizeUtf8String(
                                    wasm::WasmException::kRuntimeValuesStr))
            .ToHandle(&values_obj)) {
      if (values_obj->IsJSTypedArray()) {
        Handle<JSTypedArray> values = Handle<JSTypedArray>::cast(values_obj);
        CHECK_EQ(values->type(), kExternalUint16Array);
        CONVERT_SMI_ARG_CHECKED(index, 0);
        CHECK_LT(index, Smi::ToInt(values->length()));
        CONVERT_SMI_ARG_CHECKED(value, 1);
        auto* vals =
            reinterpret_cast<uint16_t*>(values->GetBuffer()->allocation_base());
        vals[index] = static_cast<uint16_t>(value);
      }
    }
  }
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmRunInterpreter) {
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  CONVERT_NUMBER_CHECKED(int32_t, func_index, Int32, args[0]);
  CONVERT_ARG_HANDLE_CHECKED(Object, arg_buffer_obj, 1);
  Handle<WasmInstanceObject> instance(GetWasmInstanceOnStackTop(isolate));

  // The arg buffer is the raw pointer to the caller's stack. It looks like a
  // Smi (lowest bit not set, as checked by IsSmi), but is no valid Smi. We just
  // cast it back to the raw pointer.
  CHECK(!arg_buffer_obj->IsHeapObject());
  CHECK(arg_buffer_obj->IsSmi());
  uint8_t* arg_buffer = reinterpret_cast<uint8_t*>(*arg_buffer_obj);
  ClearThreadInWasmScope wasm_flag(true);

  // Set the current isolate's context.
  DCHECK_NULL(isolate->context());
  isolate->set_context(instance->compiled_module()->native_context());

  // Find the frame pointer of the interpreter entry.
  Address frame_pointer = 0;
  {
    StackFrameIterator it(isolate, isolate->thread_local_top());
    // On top: C entry stub.
    DCHECK_EQ(StackFrame::EXIT, it.frame()->type());
    it.Advance();
    // Next: the wasm interpreter entry.
    DCHECK_EQ(StackFrame::WASM_INTERPRETER_ENTRY, it.frame()->type());
    frame_pointer = it.frame()->fp();
  }

  //ADD STARTS HERE
  /*
  StackTraceFrameIterator it(isolate);
  for (int i = 0; !it.done(); it.Advance()) {
    if (it.is_javascript())  printf("[%d] JS Frame\n", i);
    else if (it.is_wasm())   printf("[%d] WASM Frame\n", i); 
    else                     printf("[%d] Arguments Adaptor Frame\n", i); 
    i++;
  }
  // isolate->PrintStack(stdout); */

  /* We will check for the existence of a ArgumentsAdaptorFrame, which
   * would mean that there is an argument mismatch. This is what we
   * will use for taint */

  /*
 #define STACK_FRAME_TYPE_LIST(V)                                          \
  89   V(ENTRY, EntryFrame)                                                    \
  90   V(CONSTRUCT_ENTRY, ConstructEntryFrame)                                 \
  91   V(EXIT, ExitFrame)                                                      \
  92   V(OPTIMIZED, OptimizedFrame)                                            \
  93   V(WASM_COMPILED, WasmCompiledFrame)                                     \
  94   V(WASM_TO_JS, WasmToJsFrame)                                            \
  95   V(JS_TO_WASM, JsToWasmFrame)                                            \
  96   V(WASM_INTERPRETER_ENTRY, WasmInterpreterEntryFrame)                    \
  97   V(C_WASM_ENTRY, CWasmEntryFrame)                                        \
  98   V(INTERPRETED, InterpretedFrame)                                        \
  99   V(STUB, StubFrame)                                                      \
 100   V(BUILTIN_CONTINUATION, BuiltinContinuationFrame)                       \
 101   V(JAVA_SCRIPT_BUILTIN_CONTINUATION, JavaScriptBuiltinContinuationFrame) \
 102   V(INTERNAL, InternalFrame)                                              \
 103   V(CONSTRUCT, ConstructFrame)                                            \
 104   V(ARGUMENTS_ADAPTOR, ArgumentsAdaptorFrame)                             \
 105   V(BUILTIN, BuiltinFrame)                                                \
 106   V(BUILTIN_EXIT, BuiltinExitFrame)
  

 
 printf("=====================\n");
 printf("*CURRENT_FRAME_TRACE*\n");
 printf("=====================\n");
 for (StackFrameIterator it(isolate); !it.done(); it.Advance()) {
    StackFrame* frame = it.frame();
    switch(frame->type()) {
      case StackFrame::WASM_INTERPRETER_ENTRY: printf("wasm_interpreter\n"); break;
      case StackFrame::C_WASM_ENTRY:           printf("C_wasm_entry\n"); break;
      case StackFrame::BUILTIN:                printf("BUILTIN\n"); break;
      case StackFrame::STUB:                   printf("STUB\n"); break;
      case StackFrame::INTERPRETED:            printf("INTERPRETED\n"); break;
      case StackFrame::ARGUMENTS_ADAPTOR:      printf("ARGUMENTS_ADAPTOR\n"); break;
      case StackFrame::INTERNAL:               printf("INTERNAL\n"); break;
      case StackFrame::BUILTIN_EXIT:           printf("BUILTIN_EXIT\n"); break;
      case StackFrame::CONSTRUCT:              printf("CONSTRUCT\n"); break;
      case StackFrame::WASM_TO_JS:             printf("WASM_TO_JS\n"); break;
      case StackFrame::WASM_COMPILED:          printf("WASM_COMPILED\n"); break;
      case StackFrame::JS_TO_WASM:             printf("JS_TO_WASM\n"); break;
      case StackFrame::EXIT:                   printf("EXIT\n"); break;
      case StackFrame::ENTRY:                  printf("ENTRY\n"); break;
      default:
        printf("TYPE->%u\n", frame->type()); 
    } 
  } */
  

  /* Basically, if we see a JS_TO_WASM and then a ARGUMENTS_ADAPTOR, 
   * we know that we've overloaded the shit out of our WASM function. 
   * Let's look for this boys. 
   */
  
  struct timeval t;
  gettimeofday(&t, NULL);
  srand(static_cast<uint32_t>(t.tv_usec * t.tv_sec));
  
  bool isOverloadingWithTaint = false;
  bool previousWasJStoWASM = false;
  ArgumentsAdaptorFrame* arg_adapt_frame = nullptr;
  for (StackFrameIterator it(isolate); !it.done(); it.Advance()) {
    StackFrame* cur_frame = it.frame();
    if (cur_frame->type() == StackFrame::JS_TO_WASM) {
      previousWasJStoWASM = true;
    }
    else if (cur_frame->type() == StackFrame::ARGUMENTS_ADAPTOR) {
      // We are only overloading WASM if the PREVIOUS was 
      if (previousWasJStoWASM) {
        arg_adapt_frame = static_cast<ArgumentsAdaptorFrame*>(cur_frame);
        isOverloadingWithTaint = true;
        break;
      }
    }
    else {
      previousWasJStoWASM = false;
    } 
  }
  bool success;
  if (isOverloadingWithTaint) {
      /*
    int expected = arg_adapt_frame->ExpectedParameters(); 
    int actual = arg_adapt_frame->ActualParameters(); 
    // printf("[TAINTED] Overloaded with %d args when expecting %d |=| \n", actual, expected);
    assert(actual > expected);
       */
    // We will take up to 2*expected parameters as taint, stored in a taintarray
    std::vector<taint_t> taints = arg_adapt_frame->GetStrippedTaints();;
  /*
    std::cout<< "Taints: ";
    for (unsigned long i = 0; i < taints.size(); i++) {
      std::cout << std::bitset<32>(taints[i]) << " ";
    } 
    */
    success = instance->debug_info()->RunInterpreterTaint(frame_pointer, func_index, arg_buffer, taints);
    
  }
  else {
    success = instance->debug_info()->RunInterpreter(frame_pointer, func_index, arg_buffer);
  }
  //ADD ENDS HERE

  if (!success) {
    DCHECK(isolate->has_pending_exception());
    return isolate->heap()->exception();
  }
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmStackGuard) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(0, args.length());
  DCHECK(!trap_handler::IsTrapHandlerEnabled() ||
         trap_handler::IsThreadInWasm());

  ClearThreadInWasmScope wasm_flag(true);

  // Set the current isolate's context.
  DCHECK_NULL(isolate->context());
  isolate->set_context(GetWasmContextOnStackTop(isolate));

  // Check if this is a real stack overflow.
  StackLimitCheck check(isolate);
  if (check.JsHasOverflowed()) return isolate->StackOverflow();

  return isolate->stack_guard()->HandleInterrupts();
}

RUNTIME_FUNCTION(Runtime_WasmCompileLazy) {
  DCHECK_EQ(0, args.length());
  HandleScope scope(isolate);

  if (FLAG_wasm_jit_to_native) {
    Address new_func = wasm::CompileLazy(isolate);
    // The alternative to this is having 2 lazy compile builtins. The builtins
    // are part of the snapshot, so the flag has no impact on the codegen there.
    return reinterpret_cast<Object*>(new_func - Code::kHeaderSize +
                                     kHeapObjectTag);
  } else {
    return *wasm::CompileLazyOnGCHeap(isolate);
  }
}

}  // namespace internal
}  // namespace v8
