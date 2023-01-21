/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_INL_H_
#define ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_INL_H_

#include "local_reference_table.h"

#include "android-base/stringprintf.h"

#include "base/dumpable.h"
#include "gc_root-inl.h"
#include "obj_ptr-inl.h"
#include "verify_object.h"

namespace art {
namespace mirror {
class Object;
}  // namespace mirror

namespace jni {

// Verifies that the indirect table lookup is valid.
// Returns "false" if something looks bad.
inline bool LocalReferenceTable::IsValidReference(IndirectRef iref,
                                                     /*out*/std::string* error_msg) const {
  DCHECK(iref != nullptr);
  DCHECK_EQ(GetIndirectRefKind(iref), kLocal);
  const uint32_t top_index = segment_state_.top_index;
  uint32_t idx = ExtractIndex(iref);
  if (UNLIKELY(idx >= top_index)) {
    *error_msg = android::base::StringPrintf("deleted reference at index %u in a table of size %u",
                                             idx,
                                             top_index);
    return false;
  }
  if (UNLIKELY(table_[idx].GetReference()->IsNull())) {
    *error_msg = android::base::StringPrintf("deleted reference at index %u", idx);
    return false;
  }
  uint32_t iref_serial = DecodeSerial(reinterpret_cast<uintptr_t>(iref));
  uint32_t entry_serial = table_[idx].GetSerial();
  if (UNLIKELY(iref_serial != entry_serial)) {
    *error_msg = android::base::StringPrintf("stale reference with serial number %u v. current %u",
                                             iref_serial,
                                             entry_serial);
    return false;
  }
  return true;
}

// Make sure that the entry at "idx" is correctly paired with "iref".
inline bool LocalReferenceTable::CheckEntry(const char* what,
                                               IndirectRef iref,
                                               uint32_t idx) const {
  IndirectRef checkRef = ToIndirectRef(idx);
  if (UNLIKELY(checkRef != iref)) {
    std::string msg = android::base::StringPrintf(
        "JNI ERROR (app bug): attempt to %s stale %s %p (should be %p)",
        what,
        GetIndirectRefKindString(kLocal),
        iref,
        checkRef);
    AbortIfNoCheckJNI(msg);
    return false;
  }
  return true;
}

template<ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Object> LocalReferenceTable::Get(IndirectRef iref) const {
  DCHECK_EQ(GetIndirectRefKind(iref), kLocal);
  uint32_t idx = ExtractIndex(iref);
  DCHECK_LT(idx, segment_state_.top_index);
  DCHECK_EQ(DecodeSerial(reinterpret_cast<uintptr_t>(iref)), table_[idx].GetSerial());
  DCHECK(!table_[idx].GetReference()->IsNull());
  ObjPtr<mirror::Object> obj = table_[idx].GetReference()->Read<kReadBarrierOption>();
  VerifyObject(obj);
  return obj;
}

inline void LocalReferenceTable::Update(IndirectRef iref, ObjPtr<mirror::Object> obj) {
  DCHECK_EQ(GetIndirectRefKind(iref), kLocal);
  uint32_t idx = ExtractIndex(iref);
  DCHECK_LT(idx, segment_state_.top_index);
  DCHECK_EQ(DecodeSerial(reinterpret_cast<uintptr_t>(iref)), table_[idx].GetSerial());
  DCHECK(!table_[idx].GetReference()->IsNull());
  table_[idx].SetReference(obj);
}

inline void LrtEntry::Add(ObjPtr<mirror::Object> obj) {
  ++serial_;
  if (serial_ == kLRTMaxSerial) {
    serial_ = 0;
  }
  reference_ = GcRoot<mirror::Object>(obj);
}

inline void LrtEntry::SetReference(ObjPtr<mirror::Object> obj) {
  DCHECK_LT(serial_, kLRTMaxSerial);
  reference_ = GcRoot<mirror::Object>(obj);
}

}  // namespace jni
}  // namespace art

#endif  // ART_RUNTIME_JNI_LOCAL_REFERENCE_TABLE_INL_H_