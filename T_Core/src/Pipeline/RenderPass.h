#pragma once
// Aggregator for RDG-native passes (inheriting from RenderGraphPass).
// Legacy passes (TAAPass, ShadowPass, ShadowApplyPass, PathTracePass, DDGIPass)
// are excluded from the build until migrated to RenderGraphPass.

#include "Pipeline/Passes/PassCommon.h"

#include "Pipeline/Passes/GeometryPass.h"
#include "Pipeline/Passes/GBufferPass.h"
#include "Pipeline/Passes/DeferredLight.h"
#include "Pipeline/Passes/ShadowMapPass.h"
#include "Pipeline/Passes/TAAPassV2Example.h"
