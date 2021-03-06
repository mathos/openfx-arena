/*
 * This file is part of openfx-arena <https://github.com/olear/openfx-arena>,
 * Copyright (C) 2016 INRIA
 *
 * openfx-arena is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * openfx-arena is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-arena.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
*/

#include "ofxsMacros.h"
#include "ofxsMultiThread.h"
#include "ofxsImageEffect.h"
#include <Magick++.h>
#include <iostream>
#include <stdint.h>
#include <cmath>

#define kPluginName "ArcOFX"
#define kPluginGrouping "Extra/Distort"
#define kPluginIdentifier "net.fxarena.openfx.Arc"
#define kPluginVersionMajor 4
#define kPluginVersionMinor 2

#define kParamVPixel "pixel"
#define kParamVPixelLabel "Virtual Pixel"
#define kParamVPixelHint "Virtual Pixel Method"
#define kParamVPixelDefault 12

#define kParamArcAngle "angle"
#define kParamArcAngleLabel "Angle"
#define kParamArcAngleHint "Arc angle"
#define kParamArcAngleDefault 60

#define kParamArcRotate "rotate"
#define kParamArcRotateLabel "Rotate"
#define kParamArcRotateHint "Arc rotate"
#define kParamArcRotateDefault 0

#define kParamArcTopRadius "top"
#define kParamArcTopRadiusLabel "Top radius"
#define kParamArcTopRadiusHint "Arc top radius"
#define kParamArcTopRadiusDefault 0

#define kParamArcBottomRadius "bottom"
#define kParamArcBottomRadiusLabel "Bottom radius"
#define kParamArcBottomRadiusHint "Arc bottom radius"
#define kParamArcBottomRadiusDefault 0

#define kParamMatte "matte"
#define kParamMatteLabel "Matte"
#define kParamMatteHint "Merge Alpha before applying effect"
#define kParamMatteDefault false

#define kSupportsTiles 0
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading false

#define kParamOpenMP "openmp"
#define kParamOpenMPLabel "OpenMP"
#define kParamOpenMPHint "Enable/Disable OpenMP support. This will enable the plugin to use as many threads as allowed by host."
#define kParamOpenMPDefault false

#define kParamFlip "flip"
#define kParamFlipLabel "Flip"
#define kParamFlipHint "Flip image"
#define kParamFlipDefault false

using namespace OFX;

static bool _hasOpenMP = false;

class ArcPlugin : public OFX::ImageEffect
{
public:
    ArcPlugin(OfxImageEffectHandle handle);
    virtual ~ArcPlugin();
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
private:
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::ChoiceParam *vpixel_;
    OFX::DoubleParam *arcAngle_;
    OFX::DoubleParam *arcRotate_;
    OFX::DoubleParam *arcTopRadius_;
    OFX::DoubleParam *arcBottomRadius_;
    OFX::BooleanParam *matte_;
    OFX::BooleanParam *enableOpenMP_;
    OFX::BooleanParam *flip_;
};

ArcPlugin::ArcPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
{
    Magick::InitializeMagick(NULL);
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_ && dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_ && srcClip_->getPixelComponents() == OFX::ePixelComponentRGBA);

    vpixel_ = fetchChoiceParam(kParamVPixel);
    arcAngle_ = fetchDoubleParam(kParamArcAngle);
    arcRotate_ = fetchDoubleParam(kParamArcRotate);
    arcTopRadius_ = fetchDoubleParam(kParamArcTopRadius);
    arcBottomRadius_ = fetchDoubleParam(kParamArcBottomRadius);
    matte_ = fetchBooleanParam(kParamMatte);
    enableOpenMP_ = fetchBooleanParam(kParamOpenMP);
    flip_ = fetchBooleanParam(kParamFlip);

    assert(vpixel_ && arcAngle_ && arcRotate_ && arcTopRadius_&& arcBottomRadius_ && matte_ && enableOpenMP_ && flip_);
}

ArcPlugin::~ArcPlugin()
{
}

void ArcPlugin::render(const OFX::RenderArguments &args)
{
    // render scale
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // get src clip
    if (!srcClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(srcClip_);
    std::auto_ptr<const OFX::Image> srcImg(srcClip_->fetchImage(args.time));
    OfxRectI srcRod,srcBounds;
    if (srcImg.get()) {
        srcRod = srcImg->getRegionOfDefinition();
        srcBounds = srcImg->getBounds();
        if (srcImg->getRenderScale().x != args.renderScale.x ||
            srcImg->getRenderScale().y != args.renderScale.y ||
            srcImg->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
    } else {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // get dest clip
    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    assert(dstClip_);
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
    if (!dstImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
    if (dstImg->getRenderScale().x != args.renderScale.x ||
        dstImg->getRenderScale().y != args.renderScale.y ||
        dstImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }

    // get bit depth
    OFX::BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if (dstBitDepth != OFX::eBitDepthFloat || (srcImg.get() && (dstBitDepth != srcImg->getPixelDepth()))) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // get pixel component
    OFX::PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if (dstComponents != OFX::ePixelComponentRGBA || (srcImg.get() && (dstComponents != srcImg->getPixelComponents()))) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }

    // are we in the image bounds?
    OfxRectI dstBounds = dstImg->getBounds();
    if(args.renderWindow.x1 < dstBounds.x1 || args.renderWindow.x1 >= dstBounds.x2 || args.renderWindow.y1 < dstBounds.y1 || args.renderWindow.y1 >= dstBounds.y2 ||
       args.renderWindow.x2 <= dstBounds.x1 || args.renderWindow.x2 > dstBounds.x2 || args.renderWindow.y2 <= dstBounds.y1 || args.renderWindow.y2 > dstBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        return;
    }

    // get params
    int vpixel;
    double arcAngle,arcRotate,arcTopRadius,arcBottomRadius;
    bool matte = false;
    bool enableOpenMP = false;
    bool flip = false;
    vpixel_->getValueAtTime(args.time, vpixel);
    arcAngle_->getValueAtTime(args.time, arcAngle);
    arcRotate_->getValueAtTime(args.time, arcRotate);
    arcTopRadius_->getValueAtTime(args.time, arcTopRadius);
    arcBottomRadius_->getValueAtTime(args.time, arcBottomRadius);
    matte_->getValueAtTime(args.time, matte);
    enableOpenMP_->getValueAtTime(args.time, enableOpenMP);
    flip_->getValueAtTime(args.time, flip);

    // setup
    int width = srcRod.x2-srcRod.x1;
    int height = srcRod.y2-srcRod.y1;

    // OpenMP
#ifndef LEGACYIM
    unsigned int threads = 1;
    if (_hasOpenMP && enableOpenMP)
        threads = OFX::MultiThread::getNumCPUs();

    Magick::ResourceLimits::thread(threads);
#endif

    // read image
    Magick::Image image(Magick::Geometry(width,height),Magick::Color("rgba(0,0,0,0)"));
    Magick::Image output(Magick::Geometry(width,height),Magick::Color("rgba(0,0,0,1)"));
    if (srcClip_ && srcClip_->isConnected())
        image.read(width,height,"RGBA",Magick::FloatPixel,(float*)srcImg->getPixelData());

    // flip
    image.flip();

    // set virtual pixel
    switch (vpixel) {
    case 0:
        image.virtualPixelMethod(Magick::UndefinedVirtualPixelMethod);
        break;
    case 1:
        image.virtualPixelMethod(Magick::BackgroundVirtualPixelMethod);
        break;
    case 2:
        image.virtualPixelMethod(Magick::BlackVirtualPixelMethod);
        break;
    case 3:
        image.virtualPixelMethod(Magick::CheckerTileVirtualPixelMethod);
        break;
    case 4:
        image.virtualPixelMethod(Magick::DitherVirtualPixelMethod);
        break;
    case 5:
        image.virtualPixelMethod(Magick::EdgeVirtualPixelMethod);
        break;
    case 6:
        image.virtualPixelMethod(Magick::GrayVirtualPixelMethod);
        break;
    case 7:
        image.virtualPixelMethod(Magick::HorizontalTileVirtualPixelMethod);
        break;
    case 8:
        image.virtualPixelMethod(Magick::HorizontalTileEdgeVirtualPixelMethod);
        break;
    case 9:
        image.virtualPixelMethod(Magick::MirrorVirtualPixelMethod);
        break;
    case 10:
        image.virtualPixelMethod(Magick::RandomVirtualPixelMethod);
        break;
    case 11:
        image.virtualPixelMethod(Magick::TileVirtualPixelMethod);
        break;
    case 12:
        image.virtualPixelMethod(Magick::TransparentVirtualPixelMethod);
        break;
    case 13:
        image.virtualPixelMethod(Magick::VerticalTileVirtualPixelMethod);
        break;
    case 14:
        image.virtualPixelMethod(Magick::VerticalTileEdgeVirtualPixelMethod);
        break;
    case 15:
        image.virtualPixelMethod(Magick::WhiteVirtualPixelMethod);
        break;
    }

    // set args
    double arcTopRadiusRenderScale = std::floor(arcTopRadius * args.renderScale.x + 0.5);
    double arcBottomRadiusRenderScale = std::floor(arcBottomRadius * args.renderScale.x + 0.5);

    int numArgs=4;
    if (arcTopRadius==0||arcBottomRadius==0)
        numArgs=2;

    const double distortArgs1[2] = {arcAngle,arcRotate};
    const double distortArgs2[4] = {arcAngle,arcRotate,arcTopRadiusRenderScale,arcBottomRadiusRenderScale};

    // force transparent background
    image.backgroundColor(Magick::Color("rgba(0,0,0,0)"));

    // merge alpha if requested
    if (matte) {
#ifdef IM7
        image.alpha(false);
        image.alpha(true);
#else
        image.matte(false);
        image.matte(true);
#endif
    }

    // distort
    if (flip)
        image.flip();
    if (numArgs==2)
        image.distort(Magick::ArcDistortion, numArgs, distortArgs1, Magick::MagickTrue);
    else if (numArgs==4)
        image.distort(Magick::ArcDistortion, numArgs, distortArgs2, Magick::MagickTrue);
    if (flip)
        image.flip();

    // scale
    std::ostringstream scaleW;
    scaleW << width << "x";
    std::ostringstream scaleH;
    scaleH << "x" << height;
    std::size_t columns = width;
    std::size_t rows = height;
    if (image.columns()>columns)
        image.scale(scaleW.str());
    if (image.rows()>rows)
        image.scale(scaleH.str());
    image.extent(Magick::Geometry(width,height),Magick::CenterGravity);

    // flip it
    image.flip();

    // return image
    if (dstClip_ && dstClip_->isConnected()) {
        output.composite(image, 0, 0, Magick::OverCompositeOp);
#ifdef IM7
        output.composite(image, 0, 0, Magick::CopyAlphaCompositeOp);
#else
        output.composite(image, 0, 0, Magick::CopyOpacityCompositeOp);
#endif
        output.write(0,0,args.renderWindow.x2 - args.renderWindow.x1,args.renderWindow.y2 - args.renderWindow.y1,"RGBA",Magick::FloatPixel,(float*)dstImg->getPixelData());
    }
}

bool ArcPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return false;
    }
    if (srcClip_ && srcClip_->isConnected()) {
        rod = srcClip_->getRegionOfDefinition(args.time);
    } else {
        rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
        rod.x2 = rod.y2 = kOfxFlagInfiniteMax;
    }
    return true;
}

mDeclarePluginFactory(ArcPluginFactory, {}, {});

/** @brief The basic describe function, passed a plugin descriptor */
void ArcPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    size_t magickNumber;
    std::string magickString = MagickCore::GetMagickVersion(&magickNumber);
    desc.setPluginDescription("Arc Distort transform node.\n\nPowered by "+magickString+"\n\nImageMagick (R) is Copyright 1999-2015 ImageMagick Studio LLC, a non-profit organization dedicated to making software imaging solutions freely available.\n\nImageMagick is distributed under the Apache 2.0 license.");

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSupportsTiles(kSupportsTiles);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setHostFrameThreading(kHostFrameThreading);
    desc.setHostMaskingEnabled(true);
    desc.setHostMixingEnabled(true);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ArcPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum /*context*/)
{
    std::string features = MagickCore::GetMagickFeatures();
    if (features.find("OpenMP") != std::string::npos)
        _hasOpenMP = true;

    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages
    PageParamDescriptor *page = desc.definePageParam(kPluginName);
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamArcAngle);
        param->setLabel(kParamArcAngleLabel);
        param->setHint(kParamArcAngleHint);
        param->setRange(1, 360);
        param->setDisplayRange(1, 360);
        param->setDefault(kParamArcAngleDefault);
        page->addChild(*param);
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamArcRotate);
        param->setLabel(kParamArcRotateLabel);
        param->setHint(kParamArcRotateHint);
        param->setRange(0, 360);
        param->setDisplayRange(0, 360);
        param->setDefault(kParamArcRotateDefault);
        page->addChild(*param);
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamArcTopRadius);
        param->setLabel(kParamArcTopRadiusLabel);
        param->setHint(kParamArcTopRadiusHint);
        param->setRange(0, 700);
        param->setDisplayRange(0, 700);
        param->setDefault(kParamArcTopRadiusDefault);
        page->addChild(*param);
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamArcBottomRadius);
        param->setLabel(kParamArcBottomRadiusLabel);
        param->setHint(kParamArcBottomRadiusHint);
        param->setRange(0, 350);
        param->setDisplayRange(0, 350);
        param->setDefault(kParamArcBottomRadiusDefault);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFlip);
        param->setLabel(kParamFlipLabel);
        param->setHint(kParamFlipHint);
        param->setDefault(kParamFlipDefault);
        param->setAnimates(false);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamMatte);
        param->setLabel(kParamMatteLabel);
        param->setHint(kParamMatteHint);
        param->setDefault(kParamMatteDefault);
        param->setAnimates(false);
        page->addChild(*param);
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamVPixel);
        param->setLabel(kParamVPixelLabel);
        param->setHint(kParamVPixelHint);
        param->appendOption("Undefined");
        param->appendOption("Background");
        param->appendOption("Black");
        param->appendOption("CheckerTile");
        param->appendOption("Dither");
        param->appendOption("Edge");
        param->appendOption("Gray");
        param->appendOption("HorizontalTile");
        param->appendOption("HorizontalTileEdge");
        param->appendOption("Mirror");
        param->appendOption("Random");
        param->appendOption("Tile");
        param->appendOption("Transparent");
        param->appendOption("VerticalTile");
        param->appendOption("VerticalTileEdge");
        param->appendOption("White");
        param->setDefault(kParamVPixelDefault);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintDivider);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamOpenMP);
        param->setLabel(kParamOpenMPLabel);
        param->setHint(kParamOpenMPHint);
        param->setDefault(kParamOpenMPDefault);
        param->setAnimates(false);
        if (!_hasOpenMP)
            param->setEnabled(false);
        param->setLayoutHint(OFX::eLayoutHintDivider);
        page->addChild(*param);
    }
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ArcPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new ArcPlugin(handle);
}

static ArcPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
