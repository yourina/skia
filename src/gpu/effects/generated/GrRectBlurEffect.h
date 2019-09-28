/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrRectBlurEffect.fp; do not modify.
 **************************************************************************************************/
#ifndef GrRectBlurEffect_DEFINED
#define GrRectBlurEffect_DEFINED
#include "include/core/SkTypes.h"

#include "include/core/SkScalar.h"
#include "src/core/SkBlurMask.h"
#include "src/core/SkMathPriv.h"
#include "src/gpu/GrProxyProvider.h"
#include "src/gpu/GrShaderCaps.h"

#include "src/gpu/GrCoordTransform.h"
#include "src/gpu/GrFragmentProcessor.h"
class GrRectBlurEffect : public GrFragmentProcessor {
public:
    static sk_sp<GrTextureProxy> CreateBlurProfileTexture(GrProxyProvider* proxyProvider,
                                                          float sixSigma) {
        // The "profile" we are calculating is the integral of a Gaussian with 'sigma' and a half
        // plane. All such profiles are just scales of each other. So all we really care about is
        // having enough resolution so that the linear interpolation done in texture lookup doesn't
        // introduce noticeable artifacts. SkBlurMask::ComputeBlurProfile() produces profiles with
        // ceil(6 * sigma) entries. We conservatively choose to have 2 texels for each dst pixel.
        int minProfileWidth = 2 * sk_float_ceil2int(sixSigma);
        // Bin by powers of 2 with a minimum so we get good profile reuse (remember we can just
        // scale the texture coords to span the larger profile over a 6 sigma distance).
        int profileWidth = SkTMax(SkNextPow2(minProfileWidth), 32);

        static const GrUniqueKey::Domain kDomain = GrUniqueKey::GenerateDomain();
        GrUniqueKey key;
        GrUniqueKey::Builder builder(&key, kDomain, 1, "Rect Blur Mask");
        builder[0] = profileWidth;
        builder.finish();

        sk_sp<GrTextureProxy> blurProfile(proxyProvider->findOrCreateProxyByUniqueKey(
                key, GrColorType::kAlpha_8, kTopLeft_GrSurfaceOrigin));
        if (!blurProfile) {
            SkBitmap bitmap;
            if (!bitmap.tryAllocPixels(SkImageInfo::MakeA8(profileWidth, 1))) {
                return nullptr;
            }
            SkBlurMask::ComputeBlurProfile(bitmap.getAddr8(0, 0), profileWidth, profileWidth / 6.f);
            bitmap.setImmutable();
            blurProfile = proxyProvider->createProxyFromBitmap(bitmap, GrMipMapped::kNo);
            if (!blurProfile) {
                return nullptr;
            }
            SkASSERT(blurProfile->origin() == kTopLeft_GrSurfaceOrigin);
            proxyProvider->assignUniqueKeyToProxy(key, blurProfile.get());
        }

        return blurProfile;
    }

    static std::unique_ptr<GrFragmentProcessor> Make(GrProxyProvider* proxyProvider,
                                                     const GrShaderCaps& caps, const SkRect& rect,
                                                     float sigma) {
        if (!caps.floatIs32Bits()) {
            // We promote the math that gets us into the Gaussian space to full float when the rect
            // coords are large. If we don't have full float then fail. We could probably clip the
            // rect to an outset device bounds instead.
            if (SkScalarAbs(rect.fLeft) > 16000.f || SkScalarAbs(rect.fTop) > 16000.f ||
                SkScalarAbs(rect.fRight) > 16000.f || SkScalarAbs(rect.fBottom) > 16000.f) {
                return nullptr;
            }
        }

        // The profilee straddles the rect edges (half inside, half outside). Thus if the profile
        // size is greater than the rect width/height then the area at the center of the rect is
        // influenced by both edges. This is not handled by this effect.
        float profileSize = 6 * sigma;
        if (profileSize >= (float)rect.width() || profileSize >= (float)rect.height()) {
            // if the blur sigma is too large so the gaussian overlaps the whole
            // rect in either direction, fall back to CPU path for now.
            return nullptr;
        }

        auto profile = CreateBlurProfileTexture(proxyProvider, profileSize);
        if (!profile) {
            return nullptr;
        }
        // The profile is calculated such that the midpoint is at the rect's edge. To simplify
        // calculating texture coords in the shader, we inset the rect such that the profile
        // can be used with one end point aligned to the edges of the rect uniform. The texture
        // coords should be scaled such that the profile is sampled over a 6 sigma range so inset
        // by 3 sigma.
        float halfWidth = profileSize / 2;
        auto insetR = rect.makeInset(halfWidth, halfWidth);
        // inverse of the width over which the profile texture should be interpolated outward from
        // the inset rect.
        float invWidth = 1.f / profileSize;
        return std::unique_ptr<GrFragmentProcessor>(new GrRectBlurEffect(
                insetR, std::move(profile), invWidth, GrSamplerState::ClampBilerp()));
    }
    GrRectBlurEffect(const GrRectBlurEffect& src);
    std::unique_ptr<GrFragmentProcessor> clone() const override;
    const char* name() const override { return "RectBlurEffect"; }
    SkRect rect;
    TextureSampler blurProfile;
    float invProfileWidth;

private:
    GrRectBlurEffect(SkRect rect, sk_sp<GrTextureProxy> blurProfile, float invProfileWidth,
                     GrSamplerState samplerParams)
            : INHERITED(kGrRectBlurEffect_ClassID,
                        (OptimizationFlags)kCompatibleWithCoverageAsAlpha_OptimizationFlag)
            , rect(rect)
            , blurProfile(std::move(blurProfile), samplerParams)
            , invProfileWidth(invProfileWidth) {
        this->setTextureSamplerCnt(1);
    }
    GrGLSLFragmentProcessor* onCreateGLSLInstance() const override;
    void onGetGLSLProcessorKey(const GrShaderCaps&, GrProcessorKeyBuilder*) const override;
    bool onIsEqual(const GrFragmentProcessor&) const override;
    const TextureSampler& onTextureSampler(int) const override;
    GR_DECLARE_FRAGMENT_PROCESSOR_TEST
    typedef GrFragmentProcessor INHERITED;
};
#endif