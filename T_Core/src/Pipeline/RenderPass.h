#pragma once
// Aggregator. Each pass now lives in Pipeline/Passes/.
// Include order matches the original single-header order, which satisfies
// the one real inter-pass dependency (DeferredLightingPass needs DDGIPass).

#include "Pipeline/Passes/PassCommon.h"

#include "Pipeline/Passes/GeometryPass.h"
#include "Pipeline/Passes/GBufferPass.h"
#include "Pipeline/Passes/TAAPass.h"
#include "Pipeline/Passes/ShadowMapPass.h"
#include "Pipeline/Passes/ShadowPass.h"
#include "Pipeline/Passes/ShadowApplyPass.h"
#include "Pipeline/Passes/PathTracePass.h"
#include "Pipeline/Passes/DDGIPass.h"
#include "Pipeline/Passes/DeferredLightingPass.h"
