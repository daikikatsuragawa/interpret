// Copyright (c) 2018 Microsoft Corporation
// Licensed under the MIT license.
// Author: Paul Koch <code@koch.ninja>

#include "PrecompiledHeader.h"

#include <stdlib.h> // free
#include <stddef.h> // size_t, ptrdiff_t

#include "ebm_native.h" // FloatEbmType
#include "EbmInternal.h"
#include "Logging.h" // EBM_ASSERT & LOG
#include "FeatureAtomic.h"
#include "DataFrameInteraction.h"

extern bool InitializeGradients(
   const ptrdiff_t runtimeLearningTypeOrCountTargetClasses,
   const size_t cSamples,
   const void * const aTargetData,
   const FloatEbmType * const aPredictorScores,
   FloatEbmType * pGradient
);

INLINE_RELEASE_UNTEMPLATED static FloatEbmType * ConstructGradients(
   const size_t cSamples, 
   const void * const aTargetData, 
   const FloatEbmType * const aPredictorScores, 
   const ptrdiff_t runtimeLearningTypeOrCountTargetClasses
) {
   LOG_0(TraceLevelInfo, "Entered DataFrameInteraction::ConstructGradients");

   EBM_ASSERT(1 <= cSamples);
   EBM_ASSERT(nullptr != aTargetData);
   EBM_ASSERT(nullptr != aPredictorScores);
   // runtimeLearningTypeOrCountTargetClasses can only be zero if there are zero samples and we shouldn't get here
   EBM_ASSERT(0 != runtimeLearningTypeOrCountTargetClasses);

   const size_t cVectorLength = GetVectorLength(runtimeLearningTypeOrCountTargetClasses);
   EBM_ASSERT(1 <= cVectorLength);

   if(UNLIKELY(IsMultiplyError(cSamples, cVectorLength))) {
      LOG_0(TraceLevelWarning, "WARNING ConstructGradients IsMultiplyError(cSamples, cVectorLength)");
      return nullptr;
   }

   const size_t cElements = cSamples * cVectorLength;
   FloatEbmType * aGradients = EbmMalloc<FloatEbmType>(cElements);
   if(UNLIKELY(nullptr == aGradients)) {
      LOG_0(TraceLevelWarning, "WARNING ConstructGradients nullptr == aGradients");
      return nullptr;
   }

   if(UNLIKELY(InitializeGradients(
      runtimeLearningTypeOrCountTargetClasses,
      cSamples,
      aTargetData,
      aPredictorScores,
      aGradients
   ))) {
      // error already logged
      free(aGradients);
      return nullptr;
   }

   LOG_0(TraceLevelInfo, "Exited ConstructGradients");
   return aGradients;
}

INLINE_RELEASE_UNTEMPLATED static StorageDataType * * ConstructInputData(
   const size_t cFeatures, 
   const FeatureAtomic * const aFeatureAtomics, 
   const size_t cSamples, 
   const IntEbmType * const aBinnedData
) {
   LOG_0(TraceLevelInfo, "Entered DataFrameInteraction::ConstructInputData");

   EBM_ASSERT(0 < cFeatures);
   EBM_ASSERT(nullptr != aFeatureAtomics);
   EBM_ASSERT(0 < cSamples);
   EBM_ASSERT(nullptr != aBinnedData);

   StorageDataType ** const aaInputDataTo = EbmMalloc<StorageDataType *>(cFeatures);
   if(nullptr == aaInputDataTo) {
      LOG_0(TraceLevelWarning, "WARNING DataFrameInteraction::ConstructInputData nullptr == aaInputDataTo");
      return nullptr;
   }

   StorageDataType ** paInputDataTo = aaInputDataTo;
   const FeatureAtomic * pFeatureAtomic = aFeatureAtomics;
   const FeatureAtomic * const pFeatureAtomicEnd = aFeatureAtomics + cFeatures;
   do {
      StorageDataType * pInputDataTo = EbmMalloc<StorageDataType>(cSamples);
      if(nullptr == pInputDataTo) {
         LOG_0(TraceLevelWarning, "WARNING DataFrameInteraction::ConstructInputData nullptr == pInputDataTo");
         goto free_all;
      }
      *paInputDataTo = pInputDataTo;
      ++paInputDataTo;

      const IntEbmType * pInputDataFrom = &aBinnedData[pFeatureAtomic->GetIndexFeatureAtomicData() * cSamples];
      const IntEbmType * pInputDataFromEnd = &pInputDataFrom[cSamples];
      do {
         const IntEbmType inputData = *pInputDataFrom;
         if(inputData < 0) {
            LOG_0(TraceLevelError, "ERROR DataFrameInteraction::ConstructInputData inputData value cannot be negative");
            goto free_all;
         }
         if(!IsNumberConvertable<StorageDataType>(inputData)) {
            LOG_0(TraceLevelError, "ERROR DataFrameInteraction::ConstructInputData inputData value too big to reference memory");
            goto free_all;
         }
         if(!IsNumberConvertable<size_t>(inputData)) {
            LOG_0(TraceLevelError, "ERROR DataFrameInteraction::ConstructInputData inputData value too big to reference memory");
            goto free_all;
         }
         const size_t iData = static_cast<size_t>(inputData);
         if(pFeatureAtomic->GetCountBins() <= iData) {
            LOG_0(TraceLevelError, "ERROR DataFrameInteraction::ConstructInputData iData value must be less than the number of bins");
            goto free_all;
         }
         *pInputDataTo = static_cast<StorageDataType>(inputData);
         ++pInputDataTo;
         ++pInputDataFrom;
      } while(pInputDataFromEnd != pInputDataFrom);

      ++pFeatureAtomic;
   } while(pFeatureAtomicEnd != pFeatureAtomic);

   LOG_0(TraceLevelInfo, "Exited DataFrameInteraction::ConstructInputData");
   return aaInputDataTo;

free_all:
   while(aaInputDataTo != paInputDataTo) {
      --paInputDataTo;
      free(*paInputDataTo);
   }
   free(aaInputDataTo);
   return nullptr;
}

WARNING_PUSH
WARNING_DISABLE_USING_UNINITIALIZED_MEMORY
void DataFrameInteraction::Destruct() {
   LOG_0(TraceLevelInfo, "Entered DataFrameInteraction::Destruct");

   free(m_aGradients);
   if(nullptr != m_aaInputData) {
      EBM_ASSERT(1 <= m_cFeatureAtomics);
      StorageDataType ** paInputData = m_aaInputData;
      const StorageDataType * const * const paInputDataEnd = m_aaInputData + m_cFeatureAtomics;
      do {
         EBM_ASSERT(nullptr != *paInputData);
         free(*paInputData);
         ++paInputData;
      } while(paInputDataEnd != paInputData);
      free(m_aaInputData);
   }

   LOG_0(TraceLevelInfo, "Exited DataFrameInteraction::Destruct");
}
WARNING_POP

bool DataFrameInteraction::Initialize(
   const size_t cFeatureAtomics, 
   const FeatureAtomic * const aFeatureAtomics, 
   const size_t cSamples, 
   const IntEbmType * const aBinnedData, 
   const void * const aTargetData, 
   const FloatEbmType * const aPredictorScores, 
   const ptrdiff_t runtimeLearningTypeOrCountTargetClasses
) {
   EBM_ASSERT(nullptr == m_aGradients); // we expect to start with zeroed values
   EBM_ASSERT(nullptr == m_aaInputData); // we expect to start with zeroed values
   EBM_ASSERT(0 == m_cSamples); // we expect to start with zeroed values

   LOG_0(TraceLevelInfo, "Entered DataFrameInteraction::Initialize");

   if(0 != cSamples) {
      // runtimeLearningTypeOrCountTargetClasses can only be zero if 
      // there are zero samples and we shouldn't get past this point
      EBM_ASSERT(0 != runtimeLearningTypeOrCountTargetClasses);

      // if cSamples is zero, then we don't need to allocate anything since we won't use them anyways

      // check our targets since we don't use them other than for initializing
      if(IsClassification(runtimeLearningTypeOrCountTargetClasses)) {
         const IntEbmType * pTargetFrom = static_cast<const IntEbmType *>(aTargetData);
         const IntEbmType * const pTargetFromEnd = pTargetFrom + cSamples;
         const size_t countTargetClasses = static_cast<size_t>(runtimeLearningTypeOrCountTargetClasses);
         do {
            const IntEbmType data = *pTargetFrom;
            if(data < 0) {
               LOG_0(TraceLevelError, "ERROR DataFrameInteraction::Initialize target value cannot be negative");
               return true;
            }
            if(!IsNumberConvertable<StorageDataType>(data)) {
               LOG_0(TraceLevelError, "ERROR DataFrameInteraction::Initialize data target too big to reference memory");
               return true;
            }
            if(!IsNumberConvertable<size_t>(data)) {
               LOG_0(TraceLevelError, "ERROR DataFrameInteraction::Initialize data target too big to reference memory");
               return true;
            }
            const size_t iData = static_cast<size_t>(data);
            if(countTargetClasses <= iData) {
               LOG_0(TraceLevelError, "ERROR DataFrameInteraction::Initialize target value larger than number of classes");
               return true;
            }
            ++pTargetFrom;
         } while(pTargetFromEnd != pTargetFrom);
      }

      FloatEbmType * aGradients = ConstructGradients(cSamples, aTargetData, aPredictorScores, runtimeLearningTypeOrCountTargetClasses);
      if(nullptr == aGradients) {
         goto exit_error;
      }
      if(0 != cFeatureAtomics) {
         StorageDataType ** const aaInputData = ConstructInputData(cFeatureAtomics, aFeatureAtomics, cSamples, aBinnedData);
         if(nullptr == aaInputData) {
            free(aGradients);
            goto exit_error;
         }
         m_aaInputData = aaInputData;
      }
      m_aGradients = aGradients;
      m_cSamples = cSamples;
   }
   m_cFeatureAtomics = cFeatureAtomics;

   LOG_0(TraceLevelInfo, "Exited DataFrameInteraction::Initialize");
   return false;

exit_error:;
   LOG_0(TraceLevelWarning, "WARNING Exited DataFrameInteraction::Initialize");
   return true;
}
